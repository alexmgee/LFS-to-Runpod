#!/bin/bash
# ============================================================================
# LichtFeld Studio — Package Results for Download
#
# Creates a compressed archive of training outputs for easy download.
# Includes PLY/SPZ files, checkpoints, eval images, and logs.
# Excludes large intermediate files to keep the download small.
#
# Usage:
#   ./download_results.sh /workspace/output/my_scene_20260219_143000
#   ./download_results.sh   # packages all outputs
# ============================================================================
set -euo pipefail

WORKSPACE="/workspace"
OUTPUT_BASE="${WORKSPACE}/output"

if [ $# -ge 1 ]; then
    TARGET="$1"
else
    TARGET="${OUTPUT_BASE}"
fi

if [ ! -d "$TARGET" ]; then
    echo "ERROR: Directory not found: $TARGET"
    echo "Usage: ./download_results.sh [output_directory]"
    exit 1
fi

SCENE_NAME=$(basename "$TARGET")
ARCHIVE="${WORKSPACE}/${SCENE_NAME}_results.tar.gz"

echo "Packaging results from: ${TARGET}"
echo "Archive: ${ARCHIVE}"
echo ""

# Package important files, skip large intermediates
cd "$(dirname "$TARGET")"
tar -czf "${ARCHIVE}" \
    --exclude='*.bin' \
    --exclude='point_cloud_cache' \
    "$(basename "$TARGET")"

SIZE=$(ls -lh "${ARCHIVE}" | awk '{print $5}')
echo ""
echo "Done! Archive: ${ARCHIVE} (${SIZE})"
echo ""
echo "Download with:"
echo "  scp root@<POD_IP>:${ARCHIVE} ./"
echo ""
echo "Contents:"
tar -tzf "${ARCHIVE}" | head -30
TOTAL=$(tar -tzf "${ARCHIVE}" | wc -l)
if [ "$TOTAL" -gt 30 ]; then
    echo "... and $((TOTAL - 30)) more files"
fi
