<p align="center">
  <img src="lichtfeld2runpod.svg" alt="LichtFeld Studio → RunPod" width="400">
</p>

<h1 align="center">LichtFeld Studio on RunPod</h1>

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
| `example_config.json` | Example JSON config showing all mandatory fields. You probably won't need this — the recommended workflow is to export a config from the LichtFeld Studio GUI instead (see step 7). |

**Documentation:**

| Document | What it covers |
|----------|---------------|
| [RUNPOD_GUIDE.md](RUNPOD_GUIDE.md) | The full walkthrough. Read this if the Getting Started section below isn't enough. Covers pod setup, building, uploading, training, downloading, VRAM planning, equirectangular/360° scenes, config export, and troubleshooting. |
| [TRAINING_GUIDE.md](TRAINING_GUIDE.md) | Technical parameter reference. MCMC vs ADC strategy comparison, learning rate details, complete CLI flag table, VRAM budgets per GPU, recommended commands by scene type, and troubleshooting. |

## Getting Started

### 1. Create a persistent volume

Before creating any pods, create a **persistent volume** in RunPod. This is a network disk that mounts at `/workspace` and persists across pod sessions. Everything important lives here — the compiled binary, vcpkg dependencies, your datasets, and training outputs.

Go to RunPod → **Storage** → **New Network Volume**.

- **Size:** The build takes ~40 GB. Add space for your datasets and outputs. 100 GB is enough for a single scene; 300 GB if you plan to train multiple large datasets.
- **Region:** Pick the same region where you'll create pods (you can only attach a volume to pods in the same region).

### 2. Start a pod for building

You have two options for the initial build:

**Option A: Start on a CPU pod to save money (recommended)**

The most expensive part of the build is vcpkg dependency compilation (~20–40 min). This doesn't need CUDA and can run on a CPU pod at ~$0.12/hr. Dataset uploads (which can take hours for large scenes) can also happen on the same CPU pod.

Go to RunPod → **GPU Cloud** → **Deploy** → **CPU**.

- **Template:** Ubuntu 22.04 (CPU pods only offer Ubuntu templates — PyTorch templates are GPU-only)
- **Container disk:** Default is fine
- **Persistent volume:** Attach the volume you created in step 1
- **Region:** Must match your volume's region

> **What works on CPU, what doesn't:** `setup.sh` will install system packages, GCC, CMake, vcpkg, clone the source, and install all vcpkg dependencies (the slow 20–40 min part). It will then exit gracefully when CMake can't find the CUDA toolkit — that's expected. Upload your datasets while still on this pod, then terminate it. When you create a GPU pod on the same volume and re-run `setup.sh`, the cached work means it completes in just a few minutes.

**Option B: Build directly on a GPU pod (simplest)**

If you'd rather not deal with two pods, skip the CPU pod and do everything on a GPU pod. Jump to step 6 to create your GPU pod first, then come back to steps 3–5 to clone, build, and upload on it. The build takes 25–40 minutes at GPU rates (~$0.50–1.00 extra), but everything works in one step.

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

The build runs through 8 stages (0–7). Each one reports pass/fail:

0. Pre-flight checks (disk space, CUDA, internet)
1. System packages + GCC 13+ + CMake 3.30+
2. Node.js + Claude Code (optional)
3. vcpkg package manager
4. Clone + patch LichtFeld Studio source
5. CMake configure + vcpkg dependency install (this is the slow part: 15–30 min)
6. Compile LichtFeld Studio
7. Workspace setup

Known issues (x264 vcpkg regression, old GCC) are patched automatically. If a stage fails, fix the issue and re-run — the script skips completed stages.

> **CPU pod users:** The script will exit gracefully partway through stage 5 — vcpkg dependencies install successfully (the slow 20–40 min part), but then CMake fails because there's no CUDA toolkit. This is expected and the script tells you what to do next. Switch to a GPU pod, re-run `setup.sh`, and the cached vcpkg work means it finishes in a few minutes.

When it finishes, verify the binary exists:

```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --help
```

### 5. Upload your dataset

Upload your dataset while you're on a CPU pod (if using Option B) or before starting training on a GPU pod. Uploading on a CPU pod avoids paying GPU rates during what can be a long transfer.

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

If you used a CPU pod (Option B), terminate it now — the build prep and dataset are on your volume. Create a GPU pod attached to the same volume.

Go to RunPod → **GPU Cloud** → **Deploy** → choose a GPU:

| GPU | VRAM | Est. $/hr | Good for |
|-----|------|-----------|----------|
| RTX 5090 | 32 GB | ~$0.80 | Small scenes, budget runs, up to ~4M Gaussians |
| RTX 6000 Ada | 48 GB | ~$0.74 | Most scenes, up to ~10M Gaussians |
| A100 80GB | 80 GB | ~$1.39 | Large scenes, 15–20M Gaussians |
| RTX PRO 6000 | 96 GB | ~$1.69 | Very large scenes, 20M+ Gaussians |

*Prices are estimates and vary by availability. Check RunPod for current rates.*

- **Template:** RunPod PyTorch 2.x (has CUDA toolkit pre-installed)
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

### 7. Configure and train

On RunPod there is no GUI — training is done entirely through the command line. But you can (and should) configure your training parameters in the LichtFeld Studio desktop app first, then export the config and upload it to the pod.

#### Export a config from the GUI (recommended)

The best way to configure training is through the LichtFeld Studio desktop app:

1. **Open LichtFeld Studio** on your local machine
2. **Set up your training parameters** — strategy, max-cap, quality flags, iterations, learning rates, checkpoint schedule, etc. The GUI lets you visualize and adjust everything before committing to a cloud run.
3. **Export the config:** File → Export Config (saves a `.json` file)
4. **Upload the config to the pod:**
   ```bash
   scp -P <PORT> -i ~/.ssh/id_ed25519 my_config.json root@<POD_IP>:/workspace/configs/
   ```

This eliminates hand-written JSON errors. The GUI exports all mandatory fields correctly, and you get to tune parameters visually before paying for GPU time.

> **Why not hand-write JSON?** LichtFeld's config format requires **all 13 mandatory fields** — a partial config crashes with a "type must be number" error. The GUI handles this automatically. See `example_config.json` for the full template if you need to write one manually.

#### Run training

Always run training inside tmux so it survives SSH disconnections:

```bash
# On the pod
tmux new -s train

cd /workspace/runpod
./train.sh \
  --data-path /workspace/datasets/my_scene \
  --config /workspace/configs/my_config.json \
  --steps-scaler 4.67
```

To detach from tmux (training keeps running): press `Ctrl+B`, then `D`. To check on it later: `tmux attach -t train`.

**Flags you still need to set per-run** (these aren't in the config file):

| Flag | How to set it |
|------|--------------|
| `--data-path` | Path to your uploaded dataset (e.g., `/workspace/datasets/my_scene`) |
| `--steps-scaler` | Your image count ÷ 300. A 500-image dataset → `1.67`. A 1,400-image dataset → `4.67`. Datasets under 300 images don't need this. |
| `--config` | Path to your exported config file |

**Other flags worth knowing:**

| Flag | When to add it |
|------|---------------|
| `--gut` | Equirectangular / 360° images — required, MCMC only ([details](RUNPOD_GUIDE.md#6-equirectangular--360-scenes)) |
| `--tile-mode 2` | Running close to VRAM limits — halves rasterization memory |
| `--max-cap N` | Max Gaussians — adjust to your GPU's VRAM. See the [VRAM planning table](RUNPOD_GUIDE.md#8-vram-planning--tile-mode). |

For the full flag reference, see [TRAINING_GUIDE.md](TRAINING_GUIDE.md). For RunPod-specific guidance on equirectangular scenes, VRAM planning, and troubleshooting, see [RUNPOD_GUIDE.md](RUNPOD_GUIDE.md).

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
