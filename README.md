# LichtFeld Studio — RunPod Cloud Training

## Quick Start

1. Create a RunPod pod (start with CPU pod for setup to save money)
2. SSH in and upload this `runpod/` folder:
   ```bash
   scp -r runpod/ root@<POD_IP>:/workspace/runpod/
   ```
3. Run setup (builds everything from source, ~20-40 min first time):
   ```bash
   cd /workspace/runpod && chmod +x *.sh && ./setup.sh
   ```
4. Upload your dataset:
   ```bash
   rsync -avz --progress ./my_scene root@<POD_IP>:/workspace/datasets/
   ```
5. Switch to a GPU pod (same volume), then train:
   ```bash
   tmux new -s train
   cd /workspace/runpod
   ./train.sh --data-path /workspace/datasets/my_scene --strategy mcmc --iter 30000 --eval --test-every 8
   ```
6. Download results:
   ```bash
   scp -r root@<POD_IP>:/workspace/output/my_scene_* ./results/
   ```

## Files

| File | Purpose |
|------|---------|
| `setup.sh` | One-time pod setup with staged verification |
| `train.sh` | Headless training wrapper |
| `download_results.sh` | Package results for download |
| `CLAUDE.md` | Context for Claude Code on the pod |
| `TRAINING_GUIDE.md` | Complete parameter reference |

## Pod Settings

- **GPU:** RTX 6000 Ada 48GB ($0.74/hr) — good starting point
- **Template:** RunPod PyTorch 2.x (has CUDA pre-installed)
- **Container disk:** 150GB+
- **Persistent volume:** 50GB+ mounted at `/workspace`

## Progressive Training Strategy

Don't try to optimize on the first run. Get a result first, improve later:

1. **Verify** — `--iter 1` to test dataset loading
2. **Smoke test** — `--iter 50` to prove the pipeline works
3. **Baseline** — MCMC, 30K iter, default params
4. **Improve** — add quality flags one at a time

See `TRAINING_GUIDE.md` for the full parameter reference.

## Using Claude Code on the Pod

After setup.sh completes, run `claude` in `/workspace/LichtFeld-Studio`.
Claude Code reads the CLAUDE.md and TRAINING_GUIDE.md automatically and
can help build training commands, diagnose errors, and interpret results.
