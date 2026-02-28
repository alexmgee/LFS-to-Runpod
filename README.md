# LichtFeld Studio on RunPod

Scripts and documentation for building and running [LichtFeld Studio](https://github.com/MrNeRF/LichtFeld-Studio) on RunPod cloud GPUs for headless 3DGS training.

LichtFeld Studio is a C++23/CUDA application — there's no Docker image, so it builds from source. The `setup.sh` script handles the entire process (system dependencies, vcpkg, CMake, CUDA compilation) in about 25–40 minutes. Once built on a persistent volume, the binary survives pod restarts.

## What's in this repo

| File | Purpose |
|------|---------|
| `setup.sh` | Builds LichtFeld Studio from source on a RunPod pod (7 stages, idempotent) |
| `train.sh` | Headless training wrapper with auto-generated output paths |
| `download_results.sh` | Package training outputs into a tarball for download |
| `vram_monitor.sh` | Log GPU memory usage alongside training |
| `brady_config.json` | Example JSON config for a large equirectangular scene |
| `CLAUDE.md` | Context file for using Claude Code on the pod |
| [RUNPOD_GUIDE.md](RUNPOD_GUIDE.md) | Full walkthrough — pod setup, building, uploading, training, downloading, real-world runs with costs and timings, VRAM planning, 360° scene handling, known bugs |
| [TRAINING_GUIDE.md](TRAINING_GUIDE.md) | Technical parameter reference — MCMC vs ADC, learning rates, complete CLI flag reference, VRAM budgets, recommended commands by scene type |

## Getting Started

### 1. Create a RunPod pod

- **Template:** RunPod PyTorch 2.x (has CUDA pre-installed)
- **Container disk:** Default is fine — only holds OS and apt packages
- **Persistent volume:** Mounted at `/workspace`. The build takes ~40 GB, then add space for your datasets and outputs. 100 GB for a single scene, 300 GB if you're training multiple large datasets.
- SSH is enabled by default — use the **direct TCP port** shown in the pod's connection info, not the SSH gateway ([why](RUNPOD_GUIDE.md#ssh-access-use-direct-tcp-not-the-ssh-gateway))

| GPU | VRAM | $/hr | Good for |
|-----|------|------|----------|
| RTX 6000 Ada | 48 GB | ~$0.74 | Most scenes up to ~10M Gaussians |
| A100 80GB | 80 GB | ~$1.39 | Large scenes, 15–20M Gaussians |
| RTX PRO 6000 | 96 GB | ~$1.69 | Very large scenes, 20M+ Gaussians |

You can build on a CPU pod ($0.12/hr) to save money, then switch to a GPU pod on the same volume for training.

### 2. Clone this repo and upload it to the pod

```bash
# On your local machine
git clone https://github.com/alexmgee/LFS-to-Runpod.git
cd LFS-to-Runpod
```

Find your pod's IP and SSH port in the RunPod web UI under **Connection Options → TCP Port Mappings**. It will look something like `207.xx.xx.xx` port `40582`.

```bash
# Upload everything in this repo to the pod
scp -P <PORT> -i ~/.ssh/id_ed25519 -r . root@<POD_IP>:/workspace/runpod/
```

### 3. SSH in and build

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

When it finishes, verify the binary works:

```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --help
```

See [RUNPOD_GUIDE.md § Building](RUNPOD_GUIDE.md#3-building-lichtfeld-from-source) for troubleshooting.

### 4. Upload a dataset

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
# Replace <PORT> and <POD_IP> with your pod's SSH connection info from step 1
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

### 5. Train

Always run training inside tmux so it survives SSH disconnections:

```bash
# On the pod
tmux new -s train

cd /workspace/runpod
./train.sh \
  --data-path /workspace/datasets/my_scene \
  --strategy mcmc \
  --iter 30000 \
  --steps-scaler 4.67 \
  --max-cap 8000000 \
  --eval --test-every 8
```

Detach from tmux with `Ctrl+B` then `D`. Re-attach later with `tmux attach -t train`.

> **Important:** The `--steps-scaler` flag adjusts training length for your dataset size. The CLI defaults to 1.0 (no scaling), but datasets with more than 300 images need it. Calculate as `image_count / 300`. A 1,400-image dataset needs `--steps-scaler 4.67`. Without it, your scene will be undertrained. See [RUNPOD_GUIDE.md § Steps-Scaler](RUNPOD_GUIDE.md#steps-scaler).

For equirectangular/360° scenes, add `--gut --ppisp --mask-mode ignore`. See [RUNPOD_GUIDE.md § Equirectangular](RUNPOD_GUIDE.md#6-equirectangular--360-scenes).

### 6. Download results

```bash
# From your local machine — use SCP (not tar pipe, which can produce 0-byte files)
scp -P <PORT> -i ~/.ssh/id_ed25519 \
  root@<POD_IP>:/workspace/output/my_scene_*/point_cloud/iteration_30000/point_cloud.ply \
  ./results/

# Or download the full output directory
scp -P <PORT> -i ~/.ssh/id_ed25519 -r \
  root@<POD_IP>:/workspace/output/my_scene_*/ \
  ./results/
```
