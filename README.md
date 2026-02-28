<p align="center">
  <img src="lichtfeld2runpod.svg" alt="LichtFeld Studio → RunPod" width="400">
</p>

# LichtFeld Studio on RunPod

Scripts and documentation for building and running [LichtFeld Studio](https://github.com/MrNeRF/LichtFeld-Studio) on RunPod cloud GPUs for headless 3DGS training.

LichtFeld Studio is a C++23/CUDA application — there's no Docker image, so it builds from source. The `setup.sh` script handles the entire process (system dependencies, vcpkg, CMake, CUDA compilation) in about 25–40 minutes. Once built on a persistent volume, the binary survives pod restarts.

## What's in this repo

**Scripts** — you upload these to the pod and run them in order:

| File | When to use | What it does |
|------|-------------|-------------|
| `setup.sh` | Once, when you first create a pod | Installs all dependencies and builds LichtFeld Studio from source. Takes 25–40 min. You don't need to run it again unless the build breaks or you want to update. |
| `train.sh` | Each training run | Wrapper around the LichtFeld binary. Adds `--headless`, generates an output directory, and passes your flags through. |
| `vram_monitor.sh` | During training (optional) | Run in a second terminal to log GPU memory usage every 30 seconds. Helps you figure out if you can increase `--max-cap` or need `--tile-mode 2`. |
| `download_results.sh` | After training (optional) | Packages an output folder into a `.tar.gz` for download. Alternative to downloading files individually with SCP. |

**Config:**

| File | What it's for |
|------|--------------|
| `example_config.json` | Example JSON config for parameters not available as CLI flags — learning rates, checkpoint schedules, eval intervals, etc. Pass to training with `--config example_config.json`. See [RUNPOD_GUIDE.md § JSON Config](RUNPOD_GUIDE.md#7-json-config-for-advanced-parameters). |
| `CLAUDE.md` | If you install [Claude Code](https://claude.ai/code) on the pod, this file gives it context about the training environment so it can help you build commands and debug issues. Not required. |

**Documentation:**

| Document | What it covers |
|----------|---------------|
| [RUNPOD_GUIDE.md](RUNPOD_GUIDE.md) | The full walkthrough. Read this if the Getting Started section below isn't enough. Covers pod setup, building, uploading, training, downloading, VRAM planning, equirectangular/360° scenes, JSON config, known bugs, and real-world training runs with actual costs and timings. |
| [TRAINING_GUIDE.md](TRAINING_GUIDE.md) | Technical parameter reference. MCMC vs ADC strategy comparison, learning rate details, complete CLI flag table, VRAM budgets per GPU, recommended commands by scene type, and troubleshooting. |

## Getting Started

### 1. Create a persistent volume

Before creating any pods, create a **persistent volume** in RunPod. This is a network disk that mounts at `/workspace` and persists across pod sessions. Everything important lives here — the compiled binary, vcpkg dependencies, your datasets, and training outputs.

Go to RunPod → **Storage** → **New Network Volume**.

- **Size:** The build takes ~40 GB. Add space for your datasets and outputs. 100 GB is enough for a single scene; 300 GB if you plan to train multiple large datasets.
- **Region:** Pick the same region where you'll create pods (you can only attach a volume to pods in the same region).

### 2. Create a CPU pod for building and uploading

The build takes 25–40 minutes and dataset uploads can take hours for large scenes. Neither requires a GPU. Start with a CPU pod (~$0.12/hr) to avoid paying GPU rates while you wait.

Go to RunPod → **GPU Cloud** → **Deploy** → **CPU**.

- **Template:** RunPod PyTorch 2.x (has CUDA toolkit for compilation, even on CPU pods)
- **Container disk:** Default is fine — only holds OS and apt packages
- **Persistent volume:** Attach the volume you created in step 1
- **Region:** Must match your volume's region

The CUDA toolkit is installed on the template image, so the build compiles successfully on a CPU pod. The resulting binary just won't *run* for training until you switch to a GPU pod with actual GPU drivers. That's expected — you're only using the CPU pod to compile and upload.

### 3. Clone this repo and upload it to the pod

```bash
# On your local machine
git clone https://github.com/alexmgee/LFS-to-Runpod.git
cd LFS-to-Runpod
```

Find your pod's IP and SSH port in the RunPod web UI under your pod → **Connection Options → TCP Port Mappings**. It will look something like `207.xx.xx.xx` port `40582`.

```bash
# Upload everything in this repo to the pod
scp -P <PORT> -i ~/.ssh/id_ed25519 -r . root@<POD_IP>:/workspace/runpod/
```

### 4. SSH in and build

```bash
ssh -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP>

cd /workspace/runpod
chmod +x *.sh
./setup.sh
```

The build runs through 7 verified stages. Each one reports pass/fail:

1. Pre-flight checks (disk space, CUDA, internet)
2. System packages + GCC 13+ + CMake 3.30+
3. Node.js + Claude Code (optional)
4. vcpkg package manager
5. Clone + patch LichtFeld Studio source
6. CMake configure + vcpkg dependency install (this is the slow part: 15–25 min)
7. Compile LichtFeld Studio

Known issues (x264 vcpkg regression, old GCC) are patched automatically. If a stage fails, fix the issue and re-run — the script skips completed stages.

When it finishes, verify the binary exists:

```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --help
```

> **Note:** On a CPU pod, the binary will build but won't run for training (no GPU drivers). That's expected — you just need it compiled and sitting on the volume. See [RUNPOD_GUIDE.md § Building](RUNPOD_GUIDE.md#3-building-lichtfeld-from-source) for troubleshooting.

### 5. Upload your dataset

While you're still on the CPU pod, upload your dataset. This avoids paying GPU rates during what can be a long upload.

Your dataset needs either COLMAP format (`sparse/0/` with `cameras.bin`, `images.bin`, `points3D.bin` + an `images/` folder) or LichtFeld export format (`transforms.json` + `pointcloud.ply` + `images/`).

Say your dataset is at `D:\Captures\my_scene` on Windows or `/home/user/captures/my_scene` on Linux/Mac. The folder structure looks like:

```
my_scene/
  images/
    IMG_0001.jpg
    IMG_0002.jpg
    ...
  sparse/0/          ← COLMAP format
    cameras.bin
    images.bin
    points3D.bin
```

**Upload with SCP** (simplest):
```bash
# Replace <PORT> and <POD_IP> with your pod's SSH connection info from step 2
# Replace the path with the actual path to your dataset folder

# Windows (PowerShell):
scp -P <PORT> -i ~/.ssh/id_ed25519 -r "D:\Captures\my_scene" root@<POD_IP>:/workspace/datasets/

# Linux/Mac:
scp -P <PORT> -i ~/.ssh/id_ed25519 -r /home/user/captures/my_scene root@<POD_IP>:/workspace/datasets/
```

**Upload with tar pipe** (faster for large datasets — streams without creating an intermediate file):
```bash
# The -C flag sets the parent directory, and the last argument is the folder name within it.
# This uploads the folder to /workspace/datasets/my_scene on the pod.

# Windows (PowerShell):
tar -cf - -C "D:\Captures" my_scene | ssh -T -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP> "cd /workspace/datasets && tar -xf -"

# Linux/Mac:
tar -cf - -C /home/user/captures my_scene | ssh -T -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP> "cd /workspace/datasets && tar -xf -"
```

Either way, your dataset ends up at `/workspace/datasets/my_scene/` on the pod.

### 6. Switch to a GPU pod

Now that the build and dataset are on your volume, terminate the CPU pod. Then create a GPU pod attached to the same volume.

Go to RunPod → **GPU Cloud** → **Deploy** → choose a GPU:

| GPU | VRAM | Est. $/hr | Good for |
|-----|------|-----------|----------|
| RTX 5090 | 32 GB | ~$0.80 | Small scenes, budget runs, up to ~4M Gaussians |
| RTX 6000 Ada | 48 GB | ~$0.74 | Most scenes, up to ~10M Gaussians |
| A100 80GB | 80 GB | ~$1.39 | Large scenes, 15–20M Gaussians |
| RTX PRO 6000 | 96 GB | ~$1.69 | Very large scenes, 20M+ Gaussians |

*Prices are estimates and vary by availability. Check RunPod for current rates.*

- **Template:** RunPod PyTorch 2.x (same as before)
- **Persistent volume:** Attach the same volume from step 1
- **Region:** Must match your volume's region

When the pod starts, your build and dataset are already there at `/workspace`. SSH in and verify:

```bash
ssh -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP>

# Should print help text — confirms binary + GPU drivers work
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --help

# Your dataset should be here
ls /workspace/datasets/
```

### 7. Train

On RunPod there is no GUI — training is done entirely through the command line. You build a command by combining flags that control the training strategy, quality settings, and output. The `train.sh` wrapper adds `--headless` automatically and generates an output directory for you.

Here is an example command. **You will need to change the values** to match your dataset:

```bash
# On the pod — always run training inside tmux so it survives SSH disconnections
tmux new -s train

cd /workspace/runpod
./train.sh \
  --data-path /workspace/datasets/my_scene \  # ← your dataset path from step 5
  --strategy mcmc \                            # ← mcmc (bounded VRAM) or adc (more detail)
  --iter 30000 \                               # ← base iterations (scaled by steps-scaler)
  --steps-scaler 4.67 \                        # ← your image count ÷ 300 (see below)
  --max-cap 8000000 \                          # ← max Gaussians (adjust to your GPU's VRAM)
  --ppisp \                                    # ← per-pixel shading (improves appearance)
  --bilateral-grid \                           # ← exposure/white balance compensation
  --mask-mode ignore \                         # ← exclude masked regions (if masks/ exists)
  --eval --test-every 8                        # ← hold out every 8th image for evaluation
```

To detach from tmux (training keeps running): press `Ctrl+B`, then `D`. To check on it later: `tmux attach -t train`.

**Flags you need to set per-dataset:**

| Flag | How to set it |
|------|--------------|
| `--data-path` | Path to your uploaded dataset (e.g., `/workspace/datasets/my_scene`) |
| `--steps-scaler` | Your image count ÷ 300. A 500-image dataset → `1.67`. A 1,400-image dataset → `4.67`. Datasets under 300 images don't need this. |
| `--max-cap` | Depends on your GPU's VRAM. See the [VRAM planning table](RUNPOD_GUIDE.md#8-vram-planning--tile-mode). |

**Other flags worth knowing:**

| Flag | When to add it |
|------|---------------|
| `--gut` | Equirectangular / 360° images — required, MCMC only ([details](RUNPOD_GUIDE.md#6-equirectangular--360-scenes)) |
| `--tile-mode 2` | Running close to VRAM limits — halves rasterization memory |
| `--config config.json` | Set parameters that have no CLI flag — learning rates, checkpoint schedules, eval intervals, etc. ([details](RUNPOD_GUIDE.md#7-json-config-for-advanced-parameters)) |

For the full flag reference, see [TRAINING_GUIDE.md](TRAINING_GUIDE.md). For worked examples from real training runs, see [RUNPOD_GUIDE.md § Real-World Runs](RUNPOD_GUIDE.md#10-real-world-training-runs).

### 8. Download results

When training finishes, your results are on the pod at `/workspace/output/`. The `train.sh` wrapper names the output folder after your dataset with a timestamp, e.g., `/workspace/output/my_scene_20260228_143000/`.

The main file you want is the PLY — that's your trained Gaussian Splat scene. You can load it in the LichtFeld Studio desktop app (or any other splat viewer) to see the result.

First, find your output folder on the pod:
```bash
# On the pod
ls /workspace/output/
```

Then download from your local machine using SCP (use the same `<PORT>` and `<POD_IP>` from step 6):

```bash
# Download just the final PLY file
scp -P <PORT> -i ~/.ssh/id_ed25519 \
  root@<POD_IP>:/workspace/output/my_scene_20260228_143000/point_cloud/iteration_30000/point_cloud.ply \
  ./results/

# Or download everything (PLY, checkpoints, eval images, training log)
scp -P <PORT> -i ~/.ssh/id_ed25519 -r \
  root@<POD_IP>:/workspace/output/my_scene_20260228_143000/ \
  ./results/
```

> **Tip:** If you want everything as a single archive, run `./download_results.sh /workspace/output/my_scene_20260228_143000` on the pod first — it creates a `.tar.gz` excluding large intermediates, which you then SCP down. See [RUNPOD_GUIDE.md § Downloading](RUNPOD_GUIDE.md#9-downloading-results).
>
> **Warning:** Use SCP for downloads, not tar pipe (streaming `tar` directly over SSH). Tar pipe has been observed to produce 0-byte files on large PLYs.

### 9. Clean up

When you're done training, **terminate the GPU pod** to stop billing. Your volume and everything on it persists — you can spin up another pod later to train more scenes or resume a run.

If you're done for good, delete the volume too (RunPod → Storage → delete).
