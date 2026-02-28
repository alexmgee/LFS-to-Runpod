# LichtFeld Studio — Headless Training Reference

This document is the authoritative reference for building high-quality headless training
commands. If you are a Claude Code instance running on a cloud GPU, read this file before
constructing any training command.

## Binary Location

```
/workspace/LichtFeld-Studio/build/LichtFeld-Studio
```

All training commands must include `--headless`. The `train.sh` wrapper handles this
automatically, or invoke the binary directly.

## How 3DGS Training Works (Theory Summary)

A 3D Gaussian Splatting scene is millions of anisotropic 3D Gaussians, each with:
- **Position** (μ): where in 3D space
- **Covariance** (Σ = RSSᵀRᵀ): shape/orientation, parameterized as quaternion rotation + 3D scale
- **Opacity** (α via sigmoid): transparency
- **Color** (SH coefficients): view-dependent appearance via spherical harmonics

**Rendering**: Project 3D Gaussians to 2D screen space, sort by depth, alpha-blend front-to-back.
A tile-based GPU rasterizer (16×16 pixel tiles) handles this efficiently.

**Loss**: L = (1-λ)L₁ + λ(1-SSIM), where λ=0.2. Measures photometric difference between
rendered and ground-truth images.

**Optimization**: Adam optimizer (eps=1e-15) with exponential LR decay for positions.
Position LR is scaled by scene extent (spatial_lr_scale) so updates are proportional to scene size.
SH bands activate progressively every 1,000 iterations (degree 0→1→2→3).

Understanding these fundamentals helps when choosing parameters — e.g., more SH degrees = better
specular/view-dependent effects but more memory; more Gaussians = more detail but more VRAM.

## Strategy Selection: MCMC vs ADC

The two optimization strategies produce fundamentally different results. Choose based on the
scene characteristics and VRAM budget.

### MCMC (Default)

Best for: bounded VRAM, predictable output size, scenes where compactness matters.

- Fixed Gaussian budget (`--max-cap`, default 1M)
- Probabilistic relocation: dead Gaussians are recycled, not deleted
- Noise injection every iteration drives exploration (the "random walk")
- L1 opacity + scale regularization keeps Gaussians compact
- GUT mode compatible (`--gut`)
- Refines until iteration 25,000 (of 30,000 default)
- Growth target: min(max_cap, 1.05 × current_count) — grows by 5% per refinement step

### ADC (Adaptive Density Control)

Best for: maximum detail, large scenes, when VRAM is abundant (cloud GPUs).

- Grows up to 6M Gaussians by default (`--max-cap 6000000`)
- Gradient-driven: accumulates 2D screen-space position gradient norms per Gaussian.
  High gradient (≥ 0.0002) means the Gaussian is struggling to represent the scene there:
  - Small Gaussians (max scale ≤ 0.01 × scene_scale): **cloned** (duplicated)
  - Large Gaussians (max scale > threshold): **split** into 2 children (scale halved)
- Opacity reset every 3,000 iterations: clamps all opacities to ~0.01, forcing useless
  Gaussians to stay transparent and get pruned. Useful ones recover quickly via gradient.
- Prunes: opacity < 0.005, near-zero rotation (dead), oversized (> 0.1 × scene_extent)
- NOT compatible with GUT mode
- Refines until iteration 15,000 (of 30,000 default)

### Strategy Comparison

```
                    MCMC                    ADC
max_cap             1,000,000               6,000,000
stop_refine         25,000                  15,000
opacity_reg         0.01 (L1 sparsity)      0.0 (none)
scale_reg           0.01                    0.0 (none)
init_opacity        0.5                     0.1
reset_opacity       never                   every 3,000 iters
growth mechanism    probabilistic reloc     gradient-driven split/duplicate
GUT compatible      YES                     NO
noise injection     every iter (positions)  none
memory              predictable             can spike up to max_cap
```

**On cloud GPUs with 48-96GB VRAM, ADC is the quality strategy.** It can grow Gaussians
where the scene needs them most. MCMC is better when you need to control output file size,
target a specific Gaussian count, or use GUT mode.

## Learning Rate Details

Understanding LRs helps when diagnosing training issues:

| Parameter | Default LR | Schedule | Notes |
|-----------|-----------|----------|-------|
| Positions (means) | 0.000016 × scene_scale | Exponential decay to 1% by end | Only LR that decays |
| SH DC (base color) | 0.0025 | Constant | |
| SH bands 1-3 | 0.000125 (= 0.0025/20) | Constant | 1/20th of DC rate |
| Opacity | 0.025 | Constant | |
| Scaling | 0.005 | Constant | |
| Rotation | 0.001 | Constant | |
| Bilateral grid | 0.002 | Warmup 1K steps + decay | Separate optimizer |
| PPISP | 0.002 | Warmup 500 steps + decay | Separate optimizer |

Position LR is multiplied by scene extent (radius of camera distribution), so updates
are proportional to scene size. This is critical — a room-scale scene and a city-scale
scene get appropriately scaled position updates automatically.

## Dataset Format Detection

LichtFeld auto-detects two dataset formats:

1. **COLMAP format**: detected by `sparse/` or `sparse/0/` directory containing
   `cameras.bin`, `images.bin`, `points3D.bin` (binary) or `.txt` equivalents.
   Also supports `cameras.txt` at the root level.

2. **LichtFeld export format**: detected by `transforms.json` + `pointcloud.ply`.
   This is what LichtFeld's own COLMAP export produces.

Both formats expect an `images/` subdirectory with the training images.
Masks go in `masks/` if present.

## Scene Analysis Checklist

Before constructing a command, determine these properties of the dataset:

1. **Image count**: `ls /workspace/datasets/<scene>/images/ | wc -l`
2. **Image resolution**: `file /workspace/datasets/<scene>/images/*.jpg | head -3`
   (or `python3 -c "from PIL import Image; print(Image.open('<path>').size)"`)
3. **Dataset format**: check which structure is present:
   - `ls /workspace/datasets/<scene>/sparse/0/` (COLMAP binary)
   - `ls /workspace/datasets/<scene>/transforms.json` (LichtFeld export)
   - `ls /workspace/datasets/<scene>/cameras.txt` (COLMAP text)
4. **Masks present?**: `ls /workspace/datasets/<scene>/masks/`
5. **Scene type**: indoor / outdoor / object-centric / aerial / street-level
6. **Exposure variation**: were images captured with auto-exposure or fixed?
7. **Image count vs iteration scaling**: >300 images triggers auto-scaling unless
   `--steps-scaler 0` is set. Consider this when choosing `--iter`.

## Recommended Commands by Scene Profile

### Small Object Capture (50-150 images, single object)

```bash
./train.sh \
  --data-path /workspace/datasets/my_object \
  --strategy mcmc \
  --iter 30000 \
  --sh-degree 3 \
  --max-cap 500000 \
  --eval --save-eval-images \
  --test-every 8
```

VRAM: ~8-15 GB. Fast training, compact result.

### Medium Indoor Scene (100-300 images)

```bash
./train.sh \
  --data-path /workspace/datasets/my_room \
  --strategy adc \
  --iter 30000 \
  --sh-degree 3 \
  --max-cap 3000000 \
  --enable-mip \
  --eval --save-eval-images \
  --test-every 8
```

VRAM: ~20-35 GB. Mip filter helps with depth range variation (close furniture + far walls).

### Large Outdoor / Street Scene (300-800 images)

```bash
./train.sh \
  --data-path /workspace/datasets/my_street \
  --strategy adc \
  --iter 40000 \
  --steps-scaler 0 \
  --sh-degree 3 \
  --max-cap 6000000 \
  --enable-mip \
  --bilateral-grid \
  --eval --save-eval-images \
  --test-every 8
```

VRAM: ~40-60 GB. Bilateral grid compensates for exposure variation across images.
`--steps-scaler 0` disables auto-scaling so `--iter 40000` is used exactly.

### Very Large / Aerial / Drone Capture (800+ images)

```bash
./train.sh \
  --data-path /workspace/datasets/my_aerial \
  --strategy adc \
  --iter 50000 \
  --steps-scaler 0 \
  --sh-degree 3 \
  --max-cap 6000000 \
  --enable-mip \
  --bilateral-grid \
  --eval --save-eval-images \
  --test-every 16 \
  --max-width 3840
```

VRAM: ~50-80 GB. More iterations needed for convergence with many images.
`--max-width 3840` caps image resolution to manage memory. Increase `--test-every`
to keep more images for training.

### Multi-Camera / Video Capture (varying exposures per camera)

```bash
./train.sh \
  --data-path /workspace/datasets/my_video \
  --strategy adc \
  --iter 30000 \
  --sh-degree 3 \
  --max-cap 4000000 \
  --ppisp --ppisp-controller \
  --enable-mip \
  --eval --save-eval-images \
  --test-every 8
```

VRAM: ~35-55 GB. PPISP learns per-camera exposure, vignetting, color correction, and
camera response — more principled than bilateral grid for structured appearance variation.
The controller (CNN) trains in the last 5,000 iterations to predict ISP params for novel views.

### Scene with Masks (object isolation)

Add to any command above:
```bash
  --mask-mode ignore          # masked regions excluded from loss
  # or
  --mask-mode segment         # masked regions define the object
  # or
  --mask-mode alpha_consistent  # alpha-based consistency
```

If masks are inverted (background=white instead of black): add `--invert-masks`.

### Sparsity Optimization (smaller output files)

Append to any command to run a post-training sparsification phase:
```bash
  --enable-sparsity \
  --sparsify-steps 15000 \
  --prune-ratio 0.6
```

This adds 15,000 extra iterations after the main training that progressively prunes
60% of Gaussians while maintaining quality. Total iterations = `--iter` + `--sparsify-steps`.

## Complete Flag Reference

### Training Parameters

| Flag | Default | Description |
|------|---------|-------------|
| `--iter N` | 30000 | Total training iterations |
| `--strategy STR` | mcmc | `mcmc` or `adc` |
| `--sh-degree N` | 3 | Max spherical harmonics degree (0-3). Higher = better view-dependent effects |
| `--sh-degree-interval N` | 1000 | Increment SH degree every N iterations |
| `--max-cap N` | 1M (mcmc) / 6M (adc) | Maximum number of Gaussians |
| `--min-opacity F` | 0.005 | Below this opacity, Gaussians are considered dead |
| `--steps-scaler F` | 1.0 | Scales all step-based parameters. Set to 0 to disable auto-scaling |
| `--tile-mode N` | 1 | 1/2/4 tiles for rasterization. Higher = less VRAM, slower |

### Dataset Options

| Flag | Default | Description |
|------|---------|-------------|
| `--data-path PATH` | (required) | Path to COLMAP dataset |
| `--output-path PATH` | auto | Output directory |
| `--images STR` | "images" | Name of images subfolder |
| `--test-every N` | 8 | Use every Nth image for evaluation |
| `--resize_factor N` | auto | Resize images by factor: 1, 2, 4, 8 |
| `--max-width N` | 3840 | Cap image width in pixels |
| `--no-cpu-cache` | false | Disable CPU-side image caching |
| `--undistort` | false | Undistort images on-the-fly |

### Quality Enhancement Flags

| Flag | Default | When to use |
|------|---------|-------------|
| `--enable-mip` | off | Scenes with large depth range, anti-aliasing |
| `--bilateral-grid` | off | Images with varying exposure / white balance |
| `--ppisp` | off | Per-camera ISP modeling (exposure, vignetting, color, CRF) |
| `--ppisp-controller` | off | Train CNN to predict ISP for novel views (implies --ppisp) |
| `--bg-modulation` | off | Prevents background color leakage into semi-transparent regions |
| `--gut` | off | GUT raytracing mode (MCMC only, not compatible with ADC) |

### Mask Options

| Flag | Default | Description |
|------|---------|-------------|
| `--mask-mode MODE` | none | `none`, `segment`, `ignore`, `alpha_consistent` |
| `--invert-masks` | off | Swap mask foreground/background |
| `--no-alpha-as-mask` | off | Don't auto-use alpha channel as mask for RGBA images |

### Output Options

| Flag | Default | Description |
|------|---------|-------------|
| `--eval` | off | Run evaluation at save steps (computes PSNR/SSIM/LPIPS) |
| `--save-eval-images` | off | Save GT vs rendered comparison images |
| `--timelapse-images FILE` | none | Render specific views as timelapse (repeatable flag) |
| `--timelapse-every N` | 50 | Render timelapse every N iterations |

### Initialization

| Flag | Default | Description |
|------|---------|-------------|
| `--init PATH` | none | Initialize from existing .ply/.spz/.sog file |
| `--random` | off | Random initialization instead of SfM points |
| `--init-num-pts N` | (auto) | Number of random init points |
| `--resume PATH` | none | Resume from checkpoint file |

### Sparsity

| Flag | Default | Description |
|------|---------|-------------|
| `--enable-sparsity` | off | Enable post-training sparsification |
| `--sparsify-steps N` | 15000 | Additional iterations for sparsification |
| `--prune-ratio F` | 0.6 | Target fraction of Gaussians to prune |

## VRAM Budget Reference

| Component | ~VRAM per 1M Gaussians |
|-----------|------------------------|
| Splat data (pos, color, opacity, scale, rot, SH3) | ~300-400 MB |
| Adam optimizer state (2 moments per param) | ~600-800 MB |
| Gradients during backward pass | ~300-400 MB |
| Rasterization buffers (depends on image resolution) | ~500 MB - 2 GB |
| Bilateral grid (16x16x8 per image) | ~2 MB per image |
| PPISP parameters | ~50 MB total |

**Rough total per 1M Gaussians: ~2-3 GB** (excluding rasterization buffers).

### GPU Options on RunPod

| GPU | VRAM | $/hr (on-demand) | Best for |
|-----|------|-------------------|----------|
| RTX 6000 Ada | 48 GB | ~$0.74 | Small-medium scenes, budget runs |
| A100 80GB SXM | 80 GB | ~$1.39 | Large scenes, high Gaussian counts |
| RTX PRO 6000 | 96 GB | ~$1.69 | Very large scenes, maximum headroom |
| RTX PRO 6000 WK | 96 GB | ~$1.69 | Same as above (lower-power variant, ~5-14% slower) |

### Max Gaussians by GPU

| GPU | VRAM | Comfortable max Gaussians | Max with tile-mode 2 |
|-----|------|--------------------------|----------------------|
| 3090 Ti (local) | 24 GB | ~3-4M | ~5-6M |
| RTX 6000 Ada | 48 GB | ~8-10M | ~14-16M |
| A100 80GB | 80 GB | ~15-20M | ~25M+ |
| RTX PRO 6000 | 96 GB | ~20-25M | ~30M+ |

### Choosing a GPU

- **RTX 6000 Ada ($0.74/hr):** Best value. Handles most scenes that OOM on a 3090 Ti.
  Use for: indoor scenes, small-medium outdoor, object captures up to ~500 images.
- **A100 80GB ($1.39/hr):** The workhorse. High memory bandwidth (HBM2e).
  Use for: large outdoor/aerial scenes, 6M+ Gaussians, 500+ images at full resolution.
- **RTX PRO 6000 ($1.69/hr):** Maximum VRAM on newest architecture (Blackwell).
  Use for: the biggest scenes, when even 80GB isn't enough, or when you want headroom
  to experiment with max-cap and full resolution without worrying about OOM.

## Steps Scaler Behavior

When `--steps-scaler` is a positive value (default 1.0), LichtFeld auto-scales based on
camera count: if the dataset has more than 300 images, the effective scaler becomes
`max(steps_scaler, image_count / 300)`. This proportionally scales:

- `iterations`, `start_refine`, `stop_refine`, `reset_every`, `refine_every`
- `sh_degree_interval`, `eval_steps`, `save_steps`

Set `--steps-scaler 0` to disable auto-scaling and use `--iter` exactly as specified.

## Checkpoint and Save Schedule

Default save points: iterations **7,000** and **30,000** (scaled by steps_scaler).
Default eval points: same as save points (only runs if `--eval` is passed).

Outputs written to `--output-path`:
- `point_cloud/iteration_NNNNN/point_cloud.ply` — Gaussian splat data
- `checkpoint/iteration_NNNNN.ckpt` — full checkpoint (resume-able)
- `eval/` — evaluation images and metrics (if `--eval` + `--save-eval-images`)
- `timelapse/` — timelapse renders (if `--timelapse-images` specified)

## Troubleshooting

### OOM (Out of Memory)
1. Reduce `--max-cap` (e.g., 3M instead of 6M)
2. Add `--tile-mode 2` or `--tile-mode 4`
3. Reduce `--max-width` (e.g., 1920)
4. Use `--resize_factor 2` to halve image resolution
5. Switch from ADC to MCMC (bounded memory)

### Training looks blurry / low quality
1. Ensure `--sh-degree 3` (default, but verify)
2. Increase `--iter` (try 40000-50000 for large scenes)
3. For ADC: increase `--max-cap` to allow more Gaussians
4. Add `--enable-mip` for scenes with depth variation
5. Check that images aren't being excessively downscaled (`--resize_factor 1` for full res)

### Color inconsistency across views
1. Add `--bilateral-grid` for exposure compensation
2. Or `--ppisp --ppisp-controller` for principled per-camera ISP modeling
3. Check if the capture had auto-exposure enabled

### Floaters / artifacts in empty space
1. Add `--bg-modulation` to prevent background leakage
2. If masks are available: `--mask-mode ignore`
3. For MCMC: the default regularization helps; for ADC, opacity resets handle this

### Resume after interruption
```bash
./train.sh --resume /workspace/output/my_scene_TIMESTAMP/checkpoint/iteration_7000.ckpt
```
