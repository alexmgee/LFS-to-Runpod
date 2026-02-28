# LichtFeld Studio on RunPod

Scripts and documentation for building and running [LichtFeld Studio](https://github.com/MrNeRF/LichtFeld-Studio) on RunPod cloud GPUs for headless 3DGS training.

LichtFeld Studio is a C++23/CUDA application — there's no Docker image, so it builds from source. The `setup.sh` script handles the entire process (system dependencies, vcpkg, CMake, CUDA compilation) in about 25–40 minutes. Once built on a persistent volume, the binary survives pod restarts.

## Getting Started

### 1. Create a RunPod pod

- **Template:** RunPod PyTorch 2.x (has CUDA pre-installed)
- **Container disk:** 150 GB
- **Persistent volume:** 100–300 GB mounted at `/workspace`
- **Expose TCP port 22** for SSH (don't use the SSH gateway — [it breaks on Windows](RUNPOD_GUIDE.md#ssh-access-use-direct-tcp-not-the-ssh-gateway))

| GPU | VRAM | $/hr | Good for |
|-----|------|------|----------|
| RTX 6000 Ada | 48 GB | ~$0.74 | Most scenes up to ~10M Gaussians |
| A100 80GB | 80 GB | ~$1.39 | Large scenes, 15–20M Gaussians |
| RTX PRO 6000 | 96 GB | ~$1.69 | Very large scenes, 20M+ Gaussians |

You can build on a CPU pod ($0.12/hr) to save money, then switch to a GPU pod for training.

### 2. Upload scripts and build

```bash
# From your local machine — upload this repo to the pod
scp -P <PORT> -i ~/.ssh/id_ed25519 -r ./ root@<POD_IP>:/workspace/runpod/

# SSH in
ssh -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP>

# Build LichtFeld Studio (~25-40 min first time, idempotent)
cd /workspace/runpod && chmod +x *.sh && ./setup.sh
```

The build runs through 7 verified stages: system packages, GCC 13+, CMake 3.30+, vcpkg, source clone, configure, compile. Each stage reports pass/fail. Known issues (x264 vcpkg regression, GCC version) are patched automatically.

See [RUNPOD_GUIDE.md § Building](RUNPOD_GUIDE.md#3-building-lichtfeld-from-source) for details and troubleshooting.

### 3. Upload a dataset

```bash
# Tar pipe for large datasets (saturates bandwidth)
tar -cf - -C /path/to/parent my_scene | \
  ssh -T -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP> \
  "cd /workspace/datasets && tar -xf -"
```

Datasets need COLMAP format (`sparse/0/` + `images/`) or LichtFeld export format (`transforms.json` + `pointcloud.ply` + `images/`).

### 4. Train

```bash
# Always run training inside tmux
tmux new -s train

# Example: 1,400-image equirectangular scene, 8M Gaussians
./train.sh \
  --data-path /workspace/datasets/my_scene \
  --strategy mcmc \
  --gut \
  --ppisp \
  --mask-mode ignore \
  --max-cap 8000000 \
  --steps-scaler 4.67 \
  --eval --test-every 8
```

> The `--steps-scaler` flag is easy to forget and important to get right. The CLI defaults to 1.0 (no scaling), but datasets with more than 300 images need proportionally more iterations. Calculate it as `image_count / 300`. See [RUNPOD_GUIDE.md § Steps-Scaler](RUNPOD_GUIDE.md#steps-scaler) for the full explanation.

### 5. Download results

```bash
# Use SCP for PLY files (tar pipe can produce 0-byte files on large PLYs)
scp -P <PORT> -i ~/.ssh/id_ed25519 \
  root@<POD_IP>:/workspace/output/my_scene/point_cloud/iteration_30000/point_cloud.ply \
  ./results/
```

## Documentation

| Document | What it covers |
|----------|---------------|
| **[RUNPOD_GUIDE.md](RUNPOD_GUIDE.md)** | Full walkthrough — pod setup, building, uploading, training, downloading. Includes real-world training runs with actual costs and timings, VRAM planning tables, equirectangular/360° scene handling, JSON config for advanced parameters, and known bugs. |
| **[TRAINING_GUIDE.md](TRAINING_GUIDE.md)** | Technical parameter reference — MCMC vs ADC strategy comparison, learning rate details, complete CLI flag reference, VRAM budgets per GPU, recommended commands by scene type, troubleshooting. |

## Files

| File | Purpose |
|------|---------|
| `setup.sh` | Builds LichtFeld Studio from source (7 stages, idempotent) |
| `train.sh` | Headless training wrapper with auto-generated output paths |
| `download_results.sh` | Package training outputs into a tarball for download |
| `vram_monitor.sh` | Log GPU memory usage alongside training |
| `brady_config.json` | Example JSON config for a large equirectangular scene |
| `CLAUDE.md` | Context file for Claude Code on the pod |
