# CLAUDE.md — RunPod Cloud Training Context

You are helping Alex train 3D Gaussian Splatting scenes using LichtFeld Studio on a cloud GPU.
This is a headless (no GUI) environment. All training runs via CLI.

## CRITICAL: Progressive Approach

Previous training attempts failed at build, data loading, and OOM stages. **Verify each stage
before proceeding to the next.** Do not skip stages. Do not combine stages.

```
Stage 1: Verify binary works          →  --help, --headless --help
Stage 2: Verify dataset loads         →  --iter 1 (loads data, runs 1 step)
Stage 3: Smoke test (50 iterations)   →  proves end-to-end pipeline
Stage 4: Conservative full training   →  MCMC, default params, no fancy flags
Stage 5: Quality improvements         →  add one flag at a time
```

## Essential Files

- **Binary:** `/workspace/LichtFeld-Studio/build/LichtFeld-Studio`
- **Training wrapper:** `/workspace/runpod/train.sh`
- **Training guide:** `/workspace/runpod/TRAINING_GUIDE.md`
- **Datasets:** `/workspace/datasets/`
- **Output:** `/workspace/output/`

## Stage 2: Dataset Load Test

```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio \
  --headless \
  --data-path /workspace/datasets/SCENE_NAME \
  --iter 1 \
  --output-path /workspace/output/load_test \
  2>&1 | tee /workspace/load_test.log
```

Look for: "Loaded X images", "Initialized X points". Any CUDA error = stop and debug.

## Stage 3: Smoke Test

```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio \
  --headless \
  --data-path /workspace/datasets/SCENE_NAME \
  --output-path /workspace/output/smoke_test \
  --iter 50 \
  --steps-scaler 0 \
  --eval \
  2>&1 | tee /workspace/smoke_test.log
```

Verify output files exist before proceeding.

## Stage 4: Conservative Full Training

**Start with MCMC and default parameters. No fancy flags.**

```bash
./train.sh \
  --data-path /workspace/datasets/SCENE_NAME \
  --strategy mcmc \
  --iter 30000 \
  --steps-scaler 0 \
  --eval --save-eval-images \
  --test-every 8
```

Add `--mask-mode ignore` ONLY if masks/ directory exists.

## Stage 5: Quality Improvements (one at a time)

Only after Stage 4 succeeds. Each improvement = a new training run:

1. `--enable-mip` — anti-aliasing for depth variation
2. `--bilateral-grid` — exposure compensation
3. `--strategy adc --max-cap 3000000` — more Gaussians, more detail
4. Increase `--iter 40000` — more convergence
5. `--max-cap 6000000` — maximum detail (needs 48GB+ VRAM)

## Current Datasets

| Dataset | Images | Format |
|---------|--------|--------|
| `ogallala_osmo_full` | 1,397 | LichtFeld export (transforms.json + pointcloud.ply) |
| `seward_osmo_full` | 1,549 | LichtFeld export (transforms.json + pointcloud.ply) |
| `seward_drone_statue` | 6,964 | COLMAP (sparse/0) — save for last, very large |

**Train in order:** ogallala → seward_osmo → seward_drone (smallest to largest).

## Key Rules

- Always use `--headless`
- Always use `--eval --save-eval-images --test-every 8`
- Always use `--steps-scaler 0` (disables auto-scaling, uses --iter exactly)
- Always run inside `tmux` to survive SSH disconnects
- Start with MCMC strategy (bounded memory, no OOM surprises)
- Only switch to ADC after a successful MCMC run
- If OOM: reduce `--max-cap`, add `--tile-mode 2`, reduce `--max-width`
- Check VRAM before training: `nvidia-smi`

## Show Full CLI Help

```bash
/workspace/LichtFeld-Studio/build/LichtFeld-Studio --help
```
