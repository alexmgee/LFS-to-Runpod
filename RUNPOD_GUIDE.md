# LichtFeld Studio on RunPod — Community Guide

> Training 3D Gaussian Splatting scenes on cloud GPUs using LichtFeld Studio's headless mode.
>
> Based on real training sessions (Feb 2026) with actual costs, timings, and mistakes documented so you don't repeat them.

---

## Table of Contents

1. [Introduction & Prerequisites](#1-introduction--prerequisites)
2. [RunPod Setup](#2-runpod-setup)
3. [Building LichtFeld from Source](#3-building-lichtfeld-from-source)
4. [Uploading Datasets](#4-uploading-datasets)
5. [Training Fundamentals](#5-training-fundamentals)
6. [Equirectangular / 360° Scenes](#6-equirectangular--360-scenes)
7. [JSON Config for Advanced Parameters](#7-json-config-for-advanced-parameters)
8. [VRAM Planning & Tile Mode](#8-vram-planning--tile-mode)
9. [Downloading Results](#9-downloading-results)
10. [Real-World Training Runs](#10-real-world-training-runs)
11. [Quick Reference](#11-quick-reference)

---

## 1. Introduction & Prerequisites

### What This Guide Covers

Cloud GPU training of 3D Gaussian Splatting scenes using LichtFeld Studio's headless CLI. Useful when scenes are too large for your local GPU or when you need faster iteration on bigger hardware.

LichtFeld Studio is a high-performance C++23/CUDA application that builds from source on Linux. It has no pre-built Docker image for RunPod — you'll build it once on a persistent volume, and it stays there across pod sessions.

### What You Need

- **RunPod account** with billing set up
- **SSH key pair** (ed25519 recommended) added to your RunPod account
- **A prepared dataset** — either COLMAP format (`sparse/0/` with `cameras.bin`, `images.bin`, `points3D.bin`) or LichtFeld export format (`transforms.json` + `pointcloud.ply`), with images in an `images/` subfolder
- **Basic terminal comfort** — you'll be running commands over SSH
- **Patience** — the first build takes 20–40 minutes. After that, it persists on the volume

### Cost Expectations

Based on actual training runs:

| Scene Size | GPU | Time | Cost |
|-----------|-----|------|------|
| 353 images, 4M Gaussians | RTX 5090 32GB ($0.89/hr) | ~50 min | ~$0.75 |
| 1,396 images, 8M Gaussians | RTX PRO 6000 96GB ($1.69/hr) | ~5 hours | ~$8.60 |
| 2,927 images, 16M Gaussians | RTX PRO 6000 96GB ($1.69/hr) | ~10+ hours | ~$17+ |

Add $3–8 for the initial build (one-time cost, persists on volume). You can also build on a cheap CPU pod ($0.12/hr) and switch to a GPU pod for training.

---

## 2. RunPod Setup

### Create a Persistent Volume First

Before creating any pods, create a persistent volume. This is a network disk that mounts at `/workspace` and persists across pod sessions. Everything important lives here — the compiled binary, vcpkg dependencies, your datasets, and training outputs.

Go to RunPod → **Storage** → **New Network Volume**.

- **Size:** The build takes ~40 GB. Add space for your datasets and outputs. 100 GB works for a single scene, 300 GB if you're training multiple large datasets. Volumes can be resized later (may need to stop the pod first).
- **Region:** Pick the same region where you'll create pods. Pods can only attach volumes in the same region.
- **What goes where:**
  - `/workspace/LichtFeld-Studio/` — source code and build (persists)
  - `/workspace/vcpkg/` — package manager and dependencies (persists)
  - `/workspace/datasets/` — your training data
  - `/workspace/output/` — training results

### Start with a CPU Pod

The build takes 25–40 minutes and dataset uploads can take hours for large scenes. Neither requires a GPU. Start with a CPU pod (~$0.12/hr) to avoid paying GPU rates while you wait.

Go to RunPod → **GPU Cloud** → **Deploy** → **CPU**.

- **Template:** RunPod PyTorch 2.x (has CUDA toolkit pre-installed for compilation)
- **Container disk:** Default is fine — only holds OS and apt packages
- **Persistent volume:** Attach the volume you just created
- **Region:** Must match your volume's region

The build will complete through stage 5 (CMake configure) on a CPU pod. Stage 6 (compile) also works — the CUDA toolkit is available for compilation even without a GPU. The resulting binary just won't *run* until you switch to a GPU pod with actual GPU drivers.

After building and uploading your datasets, terminate the CPU pod and create a GPU pod on the same volume for training.

### GPU Selection for Training

When you're ready to train, create a GPU pod attached to the same volume.

| GPU | VRAM | Est. $/hr | Good for |
|-----|------|-----------|----------|
| RTX 5090 | 32 GB | ~$0.80 | Small scenes, budget runs, up to ~4M Gaussians |
| RTX 6000 Ada | 48 GB | ~$0.74 | Most scenes, up to ~10M Gaussians |
| A100 80GB | 80 GB | ~$1.39 | Large scenes, 15–20M Gaussians |
| RTX PRO 6000 | 96 GB | ~$1.69 | Very large scenes, 20M+ Gaussians |

*Prices are estimates and vary by availability. Check RunPod for current rates.*

See [VRAM Planning](#8-vram-planning--tile-mode) for detailed guidance on matching GPU to scene size.

### Connecting to Your Pod

There are three ways to access your pod. You'll use at least one of the first two.

**1. SSH over direct TCP (recommended for file transfers and training)**

RunPod assigns a public IP and TCP port for SSH. Find these in the RunPod web UI under your pod → **Connection Options → TCP Port Mappings**. It will look something like `207.xx.xx.xx` port `40582`.

```bash
ssh -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP>
```

This is the connection you'll use for SCP uploads/downloads and for running training commands. It works reliably with SCP and tar pipes.

**2. Web terminal (good for quick commands and writing scripts)**

Click **Connect** on your pod in the RunPod UI to open a browser-based terminal. This is convenient for writing training scripts directly on the pod (using `nano` or heredoc), checking on progress, or running quick commands.

The web terminal doesn't support file transfers, so you'll need SSH for uploading datasets and downloading results.

**3. SSH gateway (`ssh.runpod.io`) — avoid this**

RunPod also offers an SSH gateway that proxies through `ssh.runpod.io`. This does not work reliably with Windows SCP — you'll get `subsystem request failed on channel 0` errors. Use direct TCP instead.

> **Warning about Ctrl+C:** In an SSH session, pressing `Ctrl+C` will kill whatever process is running in the foreground. If you're watching training output directly (not in tmux), `Ctrl+C` will kill the training. This is why you should always run training inside tmux — if you accidentally press `Ctrl+C` in the tmux session, you can detach with `Ctrl+B` then `D` instead. If you do kill training by accident, you can resume from the last checkpoint (see [Downloading Results](#9-downloading-results)).

---

## 3. Building LichtFeld from Source

### Upload the Setup Scripts

First, upload the `runpod/` folder from your local LichtFeld Studio clone:

```bash
scp -P <PORT> -i ~/.ssh/id_ed25519 -r runpod/ root@<POD_IP>:/workspace/runpod/
```

### Run the Build

SSH into the pod and run:

```bash
cd /workspace/runpod
chmod +x *.sh
./setup.sh
```

The script runs 7 stages, each with pass/fail verification:

| Stage | What It Does | Time |
|-------|-------------|------|
| 0 | Pre-flight checks (disk space, CUDA, internet) | seconds |
| 1 | System packages + GCC 13/14 + CMake 3.30+ | 1–3 min |
| 2 | Node.js + Claude Code (optional) | 1–2 min |
| 3 | vcpkg package manager | 1–2 min |
| 4 | Clone + patch LichtFeld Studio source | 1–2 min |
| 5 | CMake configure (triggers vcpkg dependency install) | 15–25 min |
| 6 | Build LichtFeld Studio | 5–10 min |
| 7 | Set up workspace directories | seconds |

**Total: ~25–40 minutes on first run.** The script is idempotent — safe to re-run after failures.

### Known Build Issues

**x264 vcpkg regression:** The `setup.sh` script automatically patches `vcpkg.json` to remove x264 from FFmpeg features. This works around a build failure in recent vcpkg versions. No action needed.

**GCC version:** C++23 requires GCC 13+. The script tries GCC 14 first, falls back to GCC 13. RunPod's PyTorch templates usually ship with GCC 11 or 12, so the upgrade is necessary.

**OOM during linking:** If you're building on a pod with limited RAM, the build might OOM during the final linking step. The script caps parallelism at 8 jobs, but if it still fails:
```bash
cmake --build /workspace/LichtFeld-Studio/build -j 2
```

### Verification

After a successful build, verify:

```bash
# Should print help text without crashing
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --help

# Check headless-specific flags
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --headless --help
```

### The Build Persists

Once built, the binary lives on your persistent volume at `/workspace/LichtFeld-Studio/build/LichtFeld-Studio`. You don't need to rebuild when you create a new pod on the same volume — just SSH in and start training.

---

## 4. Uploading Datasets

### Dataset Format Requirements

LichtFeld auto-detects two formats:

**COLMAP format:**
```
my_scene/
  images/          # Training images (jpg/png)
  masks/           # Optional masks (matching filenames)
  sparse/
    0/
      cameras.bin
      images.bin
      points3D.bin
```

**LichtFeld export format:**
```
my_scene/
  images/          # Training images
  masks/           # Optional masks
  transforms.json
  pointcloud.ply
```

### Upload Methods

**Tar pipe (fast, for bulk uploads):**
```bash
# From your local machine (PowerShell or bash)
tar -cf - -C "D:\parent\directory" my_scene | ssh -T -p <PORT> -i ~/.ssh/id_ed25519 root@<POD_IP> "cd /workspace/datasets && tar -xf -"
```

This streams the data without creating an intermediate archive. Saturates your upload bandwidth.

**SCP (for individual files or smaller datasets):**
```bash
scp -P <PORT> -i ~/.ssh/id_ed25519 -r "D:\path\to\my_scene" root@<POD_IP>:/workspace/datasets/
```

### Windows-Specific Gotchas

- **SSH gateway fails with Windows SCP** — always use the direct TCP exposed port
- **PowerShell quoting** — long commands with special characters can break in PowerShell. If a command looks wrong, write a script file on the pod instead (see [Training Fundamentals](#5-training-fundamentals))
- **Tailscale exit nodes** — if you use Tailscale, disconnect any exit node before large transfers. Exit nodes can throttle or break the upload

### Monitoring Upload Progress

Tar pipe has no progress indicator. From a second SSH session:

```bash
# Check incoming data size
du -sh /workspace/datasets/my_scene/

# Check if tar is still running
ps aux | grep tar
```

---

## 5. Training Fundamentals

### The Headless CLI

All cloud training uses the `--headless` flag. The `train.sh` wrapper adds this automatically:

```bash
cd /workspace/runpod
./train.sh --data-path /workspace/datasets/my_scene --strategy mcmc --iter 30000 --eval --test-every 8
```

Or invoke the binary directly:
```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio \
  --headless \
  --data-path /workspace/datasets/my_scene \
  --output-path /workspace/output/my_scene_run1 \
  --strategy mcmc \
  --iter 30000 \
  --eval --save-eval-images --test-every 8
```

### MCMC vs ADC Strategy

**MCMC** (Markov Chain Monte Carlo):
- Fixed Gaussian budget via `--max-cap` (default 1M)
- Dead Gaussians are recycled, not deleted
- Noise injection drives exploration
- **Compatible with GUT mode** (equirectangular scenes)
- Predictable VRAM usage
- Best for: bounded VRAM, 360° scenes, when you need to control output size

**ADC** (Adaptive Density Control):
- Grows Gaussians where the scene needs them (gradient-driven)
- Default max 6M Gaussians
- Opacity reset every 3,000 iterations to prune dead Gaussians
- **NOT compatible with GUT mode**
- Can spike VRAM unpredictably
- Best for: maximum detail, perspective (non-360°) scenes, when VRAM is abundant

> **Recommendation:** Start with MCMC for your first run. It's bounded, predictable, and GUT-compatible. Switch to ADC once you know your VRAM headroom.

### Steps-Scaler

The steps-scaler adjusts training length proportionally to dataset size. This is **critical** for datasets with more than 300 images.

**How it works:**
- The default 30,000 iterations was calibrated for ~300 images
- With 1,400 images, each image gets shown ~3.5× fewer times per 30K iterations
- The scaler multiplies iterations (and all step-based parameters) to compensate

**The CLI vs GUI trap:**
- **GUI** auto-computes the scaler: `max(1.0, image_count / 300)`
- **CLI** defaults to `1.0` — meaning **no scaling at all**
- If you forget to pass `--steps-scaler`, your 1,400-image scene trains for the same duration as a 300-image scene, and the result will be undertrained

**Calculating the scaler:**

```
steps-scaler = max(1.0, image_count / 300)
```

| Images | Steps-Scaler | Effective Iterations (30K base) |
|--------|-------------|-------------------------------|
| 100 | 1.0 | 30,000 |
| 300 | 1.0 | 30,000 |
| 500 | 1.67 | 50,100 |
| 1,000 | 3.33 | 99,900 |
| 1,400 | 4.67 | 140,100 |
| 3,000 | 10.0 | 300,000 |
| 7,000 | 23.33 | 699,900 |

Pass it explicitly:
```bash
--steps-scaler 4.67
```

**Alternative:** Use `--steps-scaler 0 --iter 140000` to disable auto-scaling and set iterations directly.

### tmux for Persistent Sessions

SSH disconnections kill running processes. **Always run training inside tmux.**

```bash
# Create a new session
tmux new -s train

# Run your training command inside tmux...

# Detach (training continues in background): Ctrl+B, then D
# Re-attach later:
tmux attach -t train

# List sessions:
tmux ls
```

### Real-Time Log Files

Use `stdbuf -oL` with `tee` to get real-time log output:

```bash
stdbuf -oL /workspace/LichtFeld-Studio/build/LichtFeld-Studio \
  --headless ... 2>&1 | tee /workspace/output/training.log
```

Without `stdbuf`, the log file only updates when the buffer fills (at checkpoints), making it useless for monitoring from another terminal.

> **Important:** Write training scripts directly on the pod using `nano` or heredoc, not as PowerShell one-liners piped over SSH. PowerShell mangles line breaks and quoting, which has caused failed training runs.

**Example — writing a training script on the pod:**
```bash
cat > /workspace/train_my_scene.sh << 'EOF'
#!/bin/bash
cd /workspace/runpod
stdbuf -oL ./train.sh \
  --data-path /workspace/datasets/my_scene \
  --strategy mcmc \
  --iter 30000 \
  --steps-scaler 4.67 \
  --max-cap 8000000 \
  --mask-mode ignore \
  --eval --save-eval-images --test-every 8 \
  2>&1 | tee /workspace/output/my_scene.log
EOF
chmod +x /workspace/train_my_scene.sh

# Verify it looks right
cat /workspace/train_my_scene.sh

# Run it in tmux
tmux new -s train
/workspace/train_my_scene.sh
```

### Progressive Verification

Don't jump straight to a multi-hour training run. Verify each stage:

1. **Binary works:** `LichtFeld-Studio --help`
2. **Dataset loads:** `--iter 1` (loads data, runs 1 step, exits)
3. **Smoke test:** `--iter 50 --steps-scaler 0` (proves end-to-end pipeline)
4. **Conservative run:** MCMC, 30K iter, default params
5. **Quality improvements:** Add one flag at a time in subsequent runs

---

## 6. Equirectangular / 360° Scenes

If your images are equirectangular projections (2:1 aspect ratio panoramas), you need special handling.

### Use `--gut`, NOT `--undistort`

- `--undistort` does **pinhole reprojection** — this does not work for spherical images
- `--gut` enables **GUT ray-tracing mode** — correct for equirectangular projections
- GUT is MCMC-only (not compatible with ADC)

```bash
./train.sh \
  --data-path /workspace/datasets/my_erp_scene \
  --strategy mcmc \
  --gut \
  --ppisp \
  --mask-mode ignore \
  --max-cap 4000000 \
  --steps-scaler 1.18 \
  --eval --test-every 8
```

### PPISP for Per-Pixel Shading

`--ppisp` learns per-pixel image signal processing — handling exposure, vignetting, color response, and camera-specific appearance. Recommended for 360° captures where appearance varies across the panorama.

### Bilateral Grid for Exposure Compensation

`--bilateral-grid` applies per-image color correction to handle varying exposure and white balance across images.

**Bilateral grid and PPISP are compatible — they are NOT redundant:**
1. Bilateral grid color-corrects each image first (exposure/WB compensation)
2. The corrected image then feeds into PPISP (per-pixel shading)

Use both together on in-the-wild captures with varying lighting conditions.

### Masks

If your dataset has a `masks/` directory:
```bash
--mask-mode ignore
```

This excludes masked regions from the loss function. Without it, the trainer wastes Gaussian capacity on masked regions (sky, photographer's shadow, etc.).

> **Warning:** `--mask-mode segment` can produce black artifacts. Use `ignore` unless you have a specific reason for `segment`.

### Known Bug: Eval Images Broken for GUT Scenes

**The evaluator uses the standard pinhole rasterizer, not GUT ray-tracing.** This means:

- All eval images from GUT training runs render with the **wrong projection** → they look like garbage
- PSNR/SSIM scores are computed against these broken renders → **the numbers are meaningless**
- The PLY itself is fine — the training loop uses GUT correctly

**What to do:**
- Skip `--save-eval-images` for GUT runs (the images are useless)
- Don't trust the PSNR/SSIM numbers printed at the end of GUT training
- Evaluate quality by **loading the PLY in the desktop LichtFeld Studio application**
- You can still pass `--eval` for checkpoint timing, just don't read the metrics

---

## 7. JSON Config for Advanced Parameters

The `--config config.json` flag lets you set parameters that aren't exposed as CLI flags.

### Example: Even Checkpoints + Lower Scaling LR

```json
{
  "scaling_lr": 0.0025,
  "save_steps": [50000, 100000, 150000, 200000, 250000, 292800]
}
```

```bash
./train.sh \
  --data-path /workspace/datasets/my_scene \
  --config /workspace/configs/my_config.json \
  --strategy mcmc \
  --steps-scaler 9.76 \
  ...
```

### Available JSON Parameters

| Parameter | Default | Notes |
|-----------|---------|-------|
| `means_lr` | 0.000016 | Position learning rate (multiplied by scene_scale) |
| `shs_lr` | 0.0025 | SH base color LR (higher bands = 1/20th of this) |
| `scaling_lr` | 0.005 | Scale LR — consider lowering for large scenes |
| `rotation_lr` | 0.001 | Rotation LR |
| `opacity_lr` | 0.025 | Opacity LR |
| `eval_steps` | [7000, 30000] | When to evaluate (scaled by steps-scaler) |
| `save_steps` | [7000, 30000] | When to save checkpoints (scaled by steps-scaler) |

Supports flat format or nested `{"optimization": {...}}`.

### Learning Rate Notes

LichtFeld's defaults are already tuned for large scenes:
- `means_lr` (0.000016) matches the 3DGS paper's large-scene recommendation
- Position LR decays exponentially to 1% of initial by training end
- `scaling_lr` (0.005) is 5x higher than the paper's large-scene recommendation (0.001)

For equirectangular scenes (which share characteristics with city-scale scenes), consider:
- `"scaling_lr": 0.0025` — half default, between default and paper's large-scene recommendation
- `"means_lr": 0.000008` — half default, for even more precise positioning

---

## 8. VRAM Planning & Tile Mode

### VRAM Breakdown by Component

| Component | ~VRAM per 1M Gaussians |
|-----------|------------------------|
| Splat data (position, color, opacity, scale, rotation, SH3) | ~300–400 MB |
| Adam optimizer state (2 moments per parameter) | ~600–800 MB |
| Gradients during backward pass | ~300–400 MB |
| Rasterization buffers (depends on image resolution) | ~500 MB – 2 GB |

**Additional components:**
| Component | VRAM |
|-----------|------|
| Bilateral grid (16x16x8 per image) | ~2 MB per image |
| PPISP parameters | ~50 MB total |

**Rough rule: ~2–3 GB per 1M Gaussians** (excluding rasterization buffers).

### GPU Recommendations

| GPU | VRAM | Est. $/hr | Max Gaussians | Max with tile-mode 2 | Good for |
|-----|------|-----------|---------------|---------------------|----------|
| RTX 5090 | 32 GB | ~$0.80 | ~4M | ~6M | Small scenes, budget runs |
| RTX 6000 Ada | 48 GB | ~$0.74 | ~8–10M | ~14–16M | Most scenes |
| A100 80GB | 80 GB | ~$1.39 | ~15–20M | ~25M+ | Large scenes |
| RTX PRO 6000 | 96 GB | ~$1.69 | ~20–25M | ~30M+ | Very large scenes |

*Prices are estimates and vary by availability. Check RunPod for current rates.*

### Tile Mode

`--tile-mode N` splits rasterization into N tiles, reducing peak VRAM at the cost of speed.

| Tile Mode | Rasterization VRAM | Speed Impact |
|-----------|-------------------|--------------|
| 1 (default) | Full | Fastest |
| 2 | ~Half | ~10–20% slower |
| 4 | ~Quarter | ~30–40% slower |

Use tile-mode 2 when you're close to VRAM limits. Tile-mode 4 is a last resort.

### GUT Mode VRAM Impact

GUT (ray-tracing for equirectangular images) uses significantly more VRAM than the standard rasterizer and is slower (~12 iter/s vs ~19 iter/s at 4M Gaussians on RTX 5090 32GB). Factor this into your max-cap planning:

- **RTX 5090 32GB:** ~4M Gaussians with GUT+PPISP (8M causes OOM)
- **RTX PRO 6000 96GB:** 8M+ Gaussians with GUT+PPISP works fine

### VRAM Monitoring

Run the `vram_monitor.sh` script alongside training to track GPU memory:

```bash
# In a second tmux pane or SSH session:
cd /workspace/runpod
./vram_monitor.sh /workspace/output/my_scene/vram.log 30
```

This logs VRAM usage every 30 seconds with timestamps and tracks peak usage.

---

## 9. Downloading Results

### Use SCP for PLY Files

> **Warning:** Tar pipe can produce 0-byte files for large PLYs (observed with a 947 MB file). Always use SCP for downloading individual PLY files.

```bash
# Download a specific PLY
scp -P <PORT> -i ~/.ssh/id_ed25519 \
  root@<POD_IP>:/workspace/output/my_scene/point_cloud/iteration_30000/point_cloud.ply \
  ./results/

# Download entire output directory
scp -P <PORT> -i ~/.ssh/id_ed25519 -r \
  root@<POD_IP>:/workspace/output/my_scene/ \
  ./results/
```

### Output Directory Structure

```
my_scene_20260227_143000/
  training.log                               # Full training log
  point_cloud/
    iteration_7000/point_cloud.ply            # Intermediate checkpoint
    iteration_30000/point_cloud.ply           # Final result
  checkpoints/
    checkpoint_7000.resume                    # Resumable checkpoint
    checkpoint_30000.resume                   # Final checkpoint
  eval/                                       # Eval images (if --save-eval-images)
  timelapse/                                  # Timelapse renders (if configured)
```

### Resume Checkpoints

Checkpoint `.resume` files contain full training state (Gaussians, optimizer, iteration count). To resume an interrupted run:

```bash
./train.sh --resume /workspace/output/my_scene/checkpoints/checkpoint_7000.resume
```

> **Note:** Changing `--max-cap` mid-resume may cause issues — the optimizer state is pre-allocated to the original max-cap size. Stick with the original settings when resuming.

### Batch Download with download_results.sh

If you want everything in one file instead of downloading individually with SCP, the `download_results.sh` script creates a `.tar.gz` archive on the pod. It includes PLY files, checkpoints, eval images, and logs, but excludes large intermediates (`.bin` files, point cloud cache) to keep the archive smaller. You then SCP the single archive down:

```bash
# On the pod:
cd /workspace/runpod
./download_results.sh /workspace/output/my_scene_20260227_143000

# Then download the archive:
scp -P <PORT> -i ~/.ssh/id_ed25519 \
  root@<POD_IP>:/workspace/my_scene_20260227_143000_results.tar.gz \
  ./
```

---

## 10. Real-World Training Runs

Actual results from actual sessions. Use these to calibrate your expectations for training time, VRAM usage, and quality.

### Run 1: 353 ERP Images, Indoor — First Attempt (RTX 5090 32GB)

**The scene:** 353 equirectangular images (7680x3840), indoor scene, LichtFeld export format.

| Setting | Value |
|---------|-------|
| GPU | RTX 5090 32GB ($0.89/hr) |
| Strategy | MCMC |
| Iterations | 30,000 |
| Max-cap | 1,000,000 |
| Flags | `--gut --enable-mip --ppisp` |
| Steps-scaler | Not set (default 1.0) |
| Mask-mode | Not set |

**Result:** 26 minutes. PSNR/SSIM metrics were reported but are not meaningful — GUT scenes use a ray-tracing rasterizer, but the evaluator uses pinhole projection, so the eval images don't match the actual training output. See [Section 6](#known-bug-eval-images-broken-for-gut-scenes).

**What went wrong:**
- No `--steps-scaler` — CLI default is 1.0, meaning no scaling was applied. With 353 images, the scaler should have been 1.18
- No `--mask-mode ignore` — dataset had masks that weren't being used, so the trainer wasted capacity on masked regions (sky, etc.)
- MIP filter didn't help for this scene type

### Run 2: 353 ERP Images, Indoor — OOM Crash (RTX 5090 32GB)

| Setting | Value |
|---------|-------|
| Strategy | MCMC |
| Max-cap | 8,000,000 |
| Steps-scaler | 1.18 |
| Flags | `--gut --ppisp` |

**Result:** CUDA OOM crash at 4.9M Gaussians.

**Lesson: 8M Gaussians is too much for 32GB VRAM with GUT+PPISP.** The 5090 32GB maxes out around 4M Gaussians for equirectangular scenes.

### Run 3: 353 ERP Images, Indoor — Working Run (RTX 5090 32GB)

| Setting | Value |
|---------|-------|
| Strategy | MCMC |
| Iterations | 35,400 (30K × 1.18) |
| Max-cap | 4,000,000 |
| Steps-scaler | 1.18 |
| Flags | `--gut --ppisp` |
| Mask-mode | Still missing! |

**Result:** 50 minutes, 4M splats, 947 MB PLY. (PSNR/SSIM not meaningful for GUT scenes — see Run 1 note above.)

**Lessons learned:**
- 4M is the practical limit for 32GB VRAM with GUT+PPISP
- `--mask-mode ignore` was still missing — discovered afterward
- For GUT scenes, judge quality by loading the PLY in a viewer, not by eval metrics
- PLY quality was passable but hurt by missing mask mode

### Run 4: 1,396 ERP Images, Outdoor Streetscape — Full Run (RTX PRO 6000 96GB)

**The scene:** 1,396 equirectangular images, outdoor streetscape, LichtFeld export format.

| Setting | Value |
|---------|-------|
| GPU | RTX PRO 6000 96GB ($1.69/hr) |
| Strategy | MCMC |
| Iterations | 139,500 (30K × 4.65) |
| Max-cap | 8,000,000 |
| Steps-scaler | 4.65 |
| Flags | `--gut --ppisp --mask-mode ignore --eval --save-eval-images --test-every 8` |
| Config | Default learning rates |

**Result:**

| Metric | Value |
|--------|-------|
| Final splats | 8,000,000 (hit cap) |
| Training time | 18,301s (~5 hours) |
| Speed | 7.6 iter/s |
| Peak VRAM (arena) | 2,511 MB |
| Checkpoints | 32,550 and 139,500 |
| PSNR / SSIM | Not meaningful (GUT eval bug — see [Section 6](#known-bug-eval-images-broken-for-gut-scenes)) |
| Cost | ~$8.60 |

**PLY quality:** Passable — reasonable for a first production run with correct masks. Room for improvement with tuned learning rates.

**Observations:**
- Hit the 8M cap — scene could probably benefit from higher cap
- `scaling_lr` (0.005) may be too high for this scene scale — paper recommends 0.001 for large scenes
- 7.6 iter/s is expected for GUT mode with 1,396 images at full resolution
- Eval images were garbage (GUT eval bug), but training.log confirmed convergence

### Run 5: 2,927 ERP Images, Outdoor — Planned (RTX PRO 6000 96GB)

**The scene:** 2,927 equirectangular images (7680x3840), outdoor scene, LichtFeld export format.

| Setting | Value |
|---------|-------|
| GPU | RTX PRO 6000 96GB ($1.69/hr) |
| Strategy | MCMC |
| Iterations | ~292,800 (30K × 9.76) |
| Max-cap | 16,000,000 |
| Steps-scaler | 9.76 |
| Flags | `--gut --ppisp --bilateral-grid --mask-mode ignore --tile-mode 2 --test-every 8` |
| Config | `{"scaling_lr": 0.0025, "save_steps": [50000, 100000, 150000, 200000, 250000, 292800]}` |

**Key changes from Run 4:**
- Added `--bilateral-grid` for exposure compensation (compatible with PPISP)
- `--tile-mode 2` to stay within VRAM for 16M Gaussians
- Lower `scaling_lr` (0.0025 vs 0.005 default) — closer to paper's large-scene recommendation
- Custom `save_steps` for evenly spaced checkpoints (via `--config`, for parameters with no CLI flag)

---

## 11. Quick Reference

### Steps-Scaler Calculation

```
steps-scaler = max(1.0, image_count / 300)
```

| Images | Scaler | Effective Iterations |
|--------|--------|---------------------|
| 100 | 1.0 | 30,000 |
| 353 | 1.18 | 35,400 |
| 500 | 1.67 | 50,100 |
| 1,000 | 3.33 | 99,900 |
| 1,396 | 4.65 | 139,500 |
| 1,549 | 5.16 | 154,800 |
| 2,927 | 9.76 | 292,800 |
| 6,964 | 23.21 | 696,300 |

### CLI Flag Cheat Sheet

**Essential flags (always include):**
```bash
--headless                     # Required for CLI training
--data-path /workspace/datasets/my_scene
--strategy mcmc                # or adc (perspective scenes only)
--steps-scaler <value>         # Calculate from image count!
--eval --test-every 8          # Hold out every 8th image for eval
```

**Common quality flags:**
```bash
--gut                          # Equirectangular/360° scenes (MCMC only)
--ppisp                        # Per-pixel shading (recommended for most scenes)
--bilateral-grid               # Exposure/WB compensation
--mask-mode ignore             # If masks/ directory exists
--enable-mip                   # Anti-aliasing for depth variation (not for ERP)
--max-cap 8000000              # Max Gaussians (adjust to VRAM)
```

**VRAM management:**
```bash
--tile-mode 2                  # Halve rasterization VRAM
--max-width 3840               # Cap image resolution
--resize_factor 2              # Halve all image dimensions
```

**Output control:**
```bash
--output-path /workspace/output/my_scene_run1
--save-eval-images             # Save GT vs rendered comparison images
--config /workspace/config.json  # Set parameters that have no CLI flag
```

### Troubleshooting

**OOM (CUDA out of memory):**
1. Reduce `--max-cap` (e.g., 4M instead of 8M)
2. Add `--tile-mode 2`
3. Reduce `--max-width` (e.g., 1920)
4. Use `--resize_factor 2` to halve image resolution
5. Switch from ADC to MCMC (bounded memory)

**Training looks blurry / low quality:**
1. Ensure `--sh-degree 3` (default, but verify)
2. Increase iterations (try 40K–50K base for large scenes)
3. Increase `--max-cap` to allow more Gaussians
4. Check that `--steps-scaler` is set correctly
5. Add `--enable-mip` for scenes with large depth variation

**Color inconsistency across views:**
1. Add `--bilateral-grid` for exposure compensation
2. Or `--ppisp` for principled per-camera ISP modeling
3. Both can be used together (not redundant)

**Floaters / artifacts in empty space:**
1. Add `--mask-mode ignore` (if masks are available)
2. For ADC: opacity resets every 3,000 iterations handle this
3. For MCMC: default regularization helps

**Black artifacts / garbage output:**
1. If using `--mask-mode segment`, switch to `--mask-mode ignore`
2. Check that the dataset loaded correctly (`--iter 1` test)
3. Verify image format matches expectations

**SSH disconnection kills training:**
1. Always run in tmux: `tmux new -s train`
2. Detach: `Ctrl+B`, then `D`
3. Re-attach: `tmux attach -t train`

---

## Appendix: File Reference

| File | Purpose |
|------|---------|
| `setup.sh` | One-time pod setup with 7 staged verifications |
| `train.sh` | Headless training wrapper with sensible defaults |
| `download_results.sh` | Package results for download |
| `vram_monitor.sh` | Track VRAM usage alongside training |
| `example_config.json` | Example JSON config for parameters not available as CLI flags |
| `CLAUDE.md` | Context for Claude Code on the pod |
| `TRAINING_GUIDE.md` | Complete parameter reference (technical) |
| `README.md` | Quick start for the runpod directory |

---

*Last updated: Feb 2026. Prices and GPU availability on RunPod change frequently — verify current rates before starting.*
