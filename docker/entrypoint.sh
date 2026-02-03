#!/bin/bash
set -e

echo "[ENTRYPOINT] Starting LicthFeld-Studio container..."

# Define variables
PROJECT_DIR="/home/${USER}/projects/LichtFeld-Studio"


mkdir -p "${PROJECT_DIR}/external"

exec "${@:-bash}"