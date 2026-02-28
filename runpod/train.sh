#!/bin/bash
# ============================================================================
# LichtFeld Studio — Headless Training Wrapper
#
# Wraps the LichtFeld-Studio binary with sensible defaults for cloud training.
# All CLI flags are passed through to the binary.
#
# Usage:
#   ./train.sh --data-path /workspace/datasets/my_scene
#   ./train.sh --data-path /workspace/datasets/my_scene --iter 50000 --strategy mcmc
#   ./train.sh --resume /workspace/output/my_scene/checkpoint.ckpt
#   ./train.sh --help
#
# Common flags:
#   --data-path PATH     Path to COLMAP dataset (required)
#   --output-path PATH   Output directory (default: /workspace/output/<scene_name>)
#   --iter N             Training iterations (default: 30000)
#   --strategy STR       adc or mcmc (default: adc)
#   --sh-degree N        Spherical harmonics degree 0-3 (default: 3)
#   --max-cap N          Max gaussians for MCMC strategy
#   --tile-mode N        1/2/4 — tile rendering for lower VRAM usage
#   --eval               Enable evaluation during training
#   --save-eval-images   Save GT vs rendered comparison images
#   --resume PATH        Resume from checkpoint
#   --config PATH        Load config from JSON file
#   --help               Show all available flags
# ============================================================================
set -euo pipefail

WORKSPACE="/workspace"
BINARY="${WORKSPACE}/LichtFeld-Studio/build/LichtFeld-Studio"

# --- Verify binary exists ---
if [ ! -f "${BINARY}" ]; then
    echo "ERROR: LichtFeld-Studio binary not found at ${BINARY}"
    echo "Run setup.sh first."
    exit 1
fi

# --- Pass --help through directly ---
for arg in "$@"; do
    if [ "$arg" = "--help" ] || [ "$arg" = "-h" ]; then
        "${BINARY}" --help
        exit 0
    fi
done

# --- Parse data-path to auto-generate output path ---
DATA_PATH=""
OUTPUT_PATH=""
HAS_OUTPUT=false

for i in "$@"; do
    case $i in
        --data-path) shift_next=data ;;
        --output-path) shift_next=output; HAS_OUTPUT=true ;;
        -d) shift_next=data ;;
        -o) shift_next=output; HAS_OUTPUT=true ;;
        *)
            if [ "${shift_next:-}" = "data" ]; then
                DATA_PATH="$i"
                shift_next=""
            elif [ "${shift_next:-}" = "output" ]; then
                OUTPUT_PATH="$i"
                shift_next=""
            fi
            ;;
    esac
done

# Auto-generate output path from scene name if not specified
if [ "$HAS_OUTPUT" = false ] && [ -n "$DATA_PATH" ]; then
    SCENE_NAME=$(basename "$DATA_PATH")
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTPUT_PATH="${WORKSPACE}/output/${SCENE_NAME}_${TIMESTAMP}"
    echo "Auto output path: ${OUTPUT_PATH}"
fi

# --- Pre-flight GPU check ---
echo "============================================"
echo " LichtFeld Studio — Headless Training"
echo "============================================"
echo ""
nvidia-smi --query-gpu=name,memory.total,memory.free --format=csv,noheader
echo ""

# --- Build final command ---
CMD=("${BINARY}" --headless)

# Add output path if we auto-generated one
if [ "$HAS_OUTPUT" = false ] && [ -n "$OUTPUT_PATH" ]; then
    CMD+=(--output-path "${OUTPUT_PATH}")
fi

# Pass through all user arguments
CMD+=("$@")

echo "Running: ${CMD[*]}"
echo ""

# --- Create output directory ---
if [ -n "$OUTPUT_PATH" ]; then
    mkdir -p "$OUTPUT_PATH"
fi

# --- Run training ---
# Use unbuffered output so we can tail the log
"${CMD[@]}" 2>&1 | tee "${OUTPUT_PATH:-/workspace/output}/training.log"

echo ""
echo "============================================"
echo " Training complete!"
echo "============================================"
if [ -n "$OUTPUT_PATH" ]; then
    echo " Results: ${OUTPUT_PATH}/"
    echo ""
    echo " To download results:"
    echo "   scp -r root@<POD_IP>:${OUTPUT_PATH} ./results/"
fi
echo ""
