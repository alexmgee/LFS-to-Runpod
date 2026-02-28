#!/bin/bash
# ============================================================================
# LichtFeld Studio — RunPod Setup Script
# One-time setup: installs dependencies and builds LichtFeld from source.
# Run this once when you first create a pod, or after a fresh pod restart
# if you didn't use persistent storage for the build.
#
# Usage:  chmod +x setup.sh && ./setup.sh
# Time:   ~20-40 min (mostly vcpkg dependency compilation)
#
# The script is idempotent — safe to re-run after failures. Each stage
# checks whether its work is already done before proceeding.
# ============================================================================
set -euo pipefail

# --- Configuration ---
WORKSPACE="/workspace"
LICHTFELD_DIR="${WORKSPACE}/LichtFeld-Studio"
VCPKG_DIR="${WORKSPACE}/vcpkg"
BUILD_DIR="${LICHTFELD_DIR}/build"
DATASETS_DIR="${WORKSPACE}/datasets"
OUTPUT_DIR="${WORKSPACE}/output"
REPO_URL="https://github.com/MrNeRF/LichtFeld-Studio.git"
BRANCH="master"

# --- Colors ---
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

step()  { echo -e "\n${GREEN}[SETUP]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
fail()  { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }
check() { echo -e "${CYAN}[CHECK]${NC} $1"; }

STAGE=0
pass_stage() {
    STAGE=$1
    echo -e "\n${GREEN}══════════════════════════════════════════${NC}"
    echo -e "${GREEN}  Stage $1 PASSED: $2${NC}"
    echo -e "${GREEN}══════════════════════════════════════════${NC}\n"
}

# ============================================================================
# STAGE 0: Pre-flight checks
# ============================================================================
step "Stage 0: Pre-flight checks"

# Disk space
AVAIL_GB=$(df -BG /workspace | awk 'NR==2 {print $4}' | tr -d 'G')
if [ "$AVAIL_GB" -lt 40 ]; then
    fail "Only ${AVAIL_GB}GB free on /workspace — need at least 40GB for build. Increase container disk."
fi
check "Disk space: ${AVAIL_GB}GB available ✓"

# CUDA
if [ -d "/usr/local/cuda" ]; then
    CUDA_VER=$(nvcc --version 2>/dev/null | grep "release" | awk '{print $6}' | cut -c2- || echo "unknown")
    check "CUDA: ${CUDA_VER} ✓"
else
    warn "No CUDA at /usr/local/cuda — will search for system CUDA"
    # Try to find CUDA elsewhere (common on RunPod images)
    for cuda_dir in /usr/local/cuda-* /opt/cuda; do
        if [ -f "${cuda_dir}/bin/nvcc" ]; then
            check "Found CUDA at ${cuda_dir}"
            export PATH="${cuda_dir}/bin:${PATH}"
            CUDA_VER=$(nvcc --version 2>/dev/null | grep "release" | awk '{print $6}' | cut -c2- || echo "unknown")
            check "CUDA: ${CUDA_VER} ✓"
            break
        fi
    done
fi

# GPU (optional for build, required for training)
if nvidia-smi &>/dev/null; then
    nvidia-smi --query-gpu=name,memory.total --format=csv,noheader
else
    warn "No GPU detected — build will work but training requires a GPU pod"
fi

# Internet
if curl -sI --max-time 5 https://github.com > /dev/null 2>&1; then
    check "Internet connectivity ✓"
else
    fail "Cannot reach github.com — check internet connectivity"
fi

pass_stage 0 "Environment OK"

# ============================================================================
# STAGE 1: System dependencies
# ============================================================================
step "Stage 1: Installing system packages..."
apt-get update -qq
apt-get install -y --no-install-recommends \
    build-essential \
    curl \
    git \
    ca-certificates \
    gnupg2 \
    wget \
    unzip \
    zip \
    pkg-config \
    ninja-build \
    nasm \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    python3 \
    python3-dev \
    python3-pip \
    libxinerama-dev \
    libxcursor-dev \
    xorg-dev \
    libglu1-mesa-dev \
    software-properties-common \
    tmux \
    2>&1 | tail -3

# --- GCC 13+ (required for C++23) ---
step "Checking GCC version..."
GCC_VER=$(gcc -dumpversion 2>/dev/null || echo "0")
GCC_MAJOR=$(echo "$GCC_VER" | cut -d. -f1)

if [ "$GCC_MAJOR" -ge 13 ]; then
    check "GCC ${GCC_VER} is sufficient ✓"
else
    step "Installing GCC 14..."
    if add-apt-repository -y ppa:ubuntu-toolchain-r/test 2>/dev/null; then
        apt-get update -qq
        if apt-get install -y gcc-14 g++-14 2>&1 | tail -1; then
            update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
                --slave /usr/bin/g++ g++ /usr/bin/g++-14
        else
            warn "GCC 14 failed, trying GCC 13..."
            apt-get install -y gcc-13 g++-13 2>&1 | tail -1
            update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
                --slave /usr/bin/g++ g++ /usr/bin/g++-13
        fi
    else
        warn "PPA failed, trying direct install..."
        apt-get install -y gcc-13 g++-13 2>&1 | tail -1 || fail "Cannot install GCC 13+. C++23 requires GCC 13 or later."
        update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
            --slave /usr/bin/g++ g++ /usr/bin/g++-13
    fi
fi
check "GCC: $(gcc --version | head -1)"

# --- CMake 3.30+ ---
step "Checking CMake version..."
CMAKE_NEEDED=false
if command -v cmake &>/dev/null; then
    CMAKE_VER=$(cmake --version | head -1 | awk '{print $3}')
    CMAKE_MAJOR=$(echo "$CMAKE_VER" | cut -d. -f1)
    CMAKE_MINOR=$(echo "$CMAKE_VER" | cut -d. -f2)
    if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 30 ]); then
        CMAKE_NEEDED=true
    else
        check "CMake ${CMAKE_VER} is sufficient ✓"
    fi
else
    CMAKE_NEEDED=true
fi

if [ "$CMAKE_NEEDED" = true ]; then
    step "Installing CMake 3.31..."
    wget -q https://github.com/Kitware/CMake/releases/download/v3.31.4/cmake-3.31.4-linux-x86_64.sh
    chmod +x cmake-3.31.4-linux-x86_64.sh
    ./cmake-3.31.4-linux-x86_64.sh --skip-license --prefix=/usr/local
    rm cmake-3.31.4-linux-x86_64.sh
    check "CMake: $(cmake --version | head -1)"
fi

pass_stage 1 "System dependencies installed"

# ============================================================================
# STAGE 2: Node.js + Claude Code (optional but recommended)
# ============================================================================
step "Stage 2: Installing Claude Code..."
if command -v claude &>/dev/null; then
    check "Claude Code already installed ✓"
else
    if ! command -v node &>/dev/null || [ "$(node -v | cut -d. -f1 | tr -d 'v')" -lt 20 ]; then
        curl -fsSL https://deb.nodesource.com/setup_22.x | bash -
        apt-get install -y nodejs 2>&1 | tail -1
    fi
    check "Node.js: $(node --version)"
    npm install -g @anthropic-ai/claude-code 2>&1 | tail -3
    check "Claude Code installed ✓ (authenticate on first run with: claude)"
fi

pass_stage 2 "Claude Code ready"

# ============================================================================
# STAGE 3: vcpkg
# ============================================================================
step "Stage 3: Setting up vcpkg..."
if [ -d "${VCPKG_DIR}" ] && [ -f "${VCPKG_DIR}/vcpkg" ]; then
    check "vcpkg already exists, updating..."
    cd "${VCPKG_DIR}" && git pull -q && ./bootstrap-vcpkg.sh -disableMetrics 2>&1 | tail -1
else
    git clone https://github.com/microsoft/vcpkg.git "${VCPKG_DIR}"
    cd "${VCPKG_DIR}" && ./bootstrap-vcpkg.sh -disableMetrics 2>&1 | tail -1
fi
export VCPKG_ROOT="${VCPKG_DIR}"
export PATH="${VCPKG_ROOT}:${PATH}"

# Optimize: release-only builds to save time and disk
if ! grep -q "VCPKG_BUILD_TYPE" "${VCPKG_DIR}/triplets/x64-linux.cmake" 2>/dev/null; then
    echo 'set(VCPKG_BUILD_TYPE release)' >> "${VCPKG_DIR}/triplets/x64-linux.cmake"
fi

check "vcpkg ready at ${VCPKG_DIR} ✓"
pass_stage 3 "vcpkg ready"

# ============================================================================
# STAGE 4: Clone + patch LichtFeld Studio
# ============================================================================
step "Stage 4: Getting LichtFeld Studio source..."
if [ -d "${LICHTFELD_DIR}" ]; then
    check "Source exists, pulling latest..."
    cd "${LICHTFELD_DIR}" && git fetch origin && git checkout "${BRANCH}" && git pull origin "${BRANCH}"
else
    git clone --branch "${BRANCH}" --recursive "${REPO_URL}" "${LICHTFELD_DIR}"
fi

# --- Apply known build fixes ---
step "Applying known build fixes..."
cd "${LICHTFELD_DIR}"

# Fix 1: Remove x264 from ffmpeg features (vcpkg-make regression)
if grep -q '"x264"' vcpkg.json 2>/dev/null; then
    step "Patching vcpkg.json: removing x264 from ffmpeg features..."
    python3 -c "
import json
with open('vcpkg.json', 'r') as f:
    data = json.load(f)
deps = data.get('dependencies', [])
for dep in deps:
    if isinstance(dep, dict) and dep.get('name') == 'ffmpeg':
        features = dep.get('features', [])
        if 'x264' in features:
            features.remove('x264')
            dep['features'] = features
            print('Removed x264 from ffmpeg features')
with open('vcpkg.json', 'w') as f:
    json.dump(data, f, indent=2)
    f.write('\n')
"
    check "vcpkg.json patched ✓"
else
    check "vcpkg.json already clean ✓"
fi

check "Source ready at ${LICHTFELD_DIR} ✓"
pass_stage 4 "Source code ready"

# ============================================================================
# STAGE 5: Configure (CMake + vcpkg install)
# ============================================================================
step "Stage 5: Configuring build..."
step "This triggers vcpkg dependency installation — 15-30 min on first run..."
cd "${LICHTFELD_DIR}"

# Find CUDA compiler
CUDA_COMPILER=""
for nvcc_path in /usr/local/cuda/bin/nvcc /usr/local/cuda-*/bin/nvcc /opt/cuda/bin/nvcc; do
    if [ -f "$nvcc_path" ]; then
        CUDA_COMPILER="$nvcc_path"
        break
    fi
done

if [ -z "$CUDA_COMPILER" ]; then
    CUDA_COMPILER=$(which nvcc 2>/dev/null || true)
fi

if [ -z "$CUDA_COMPILER" ]; then
    fail "Cannot find nvcc. Is CUDA installed? Check: ls /usr/local/cuda*/bin/nvcc"
fi
check "Using CUDA compiler: ${CUDA_COMPILER}"

cmake -B "${BUILD_DIR}" -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CUDA_COMPILER="${CUDA_COMPILER}" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DBUILD_PYTHON_STUBS=OFF \
    -DBUILD_CUDA_PTX_ONLY=ON \
    2>&1 | tee "${WORKSPACE}/configure_log.txt"

CONFIGURE_EXIT=$?
if [ $CONFIGURE_EXIT -ne 0 ]; then
    echo ""
    fail "CMake configure failed (exit code $CONFIGURE_EXIT). Check ${WORKSPACE}/configure_log.txt"
fi

check "CMake configure succeeded ✓"
pass_stage 5 "Build configured"

# ============================================================================
# STAGE 6: Build
# ============================================================================
step "Stage 6: Building LichtFeld Studio..."
NPROC=$(nproc)
# Conservative parallelism to avoid OOM during linking
BUILD_JOBS=$(( NPROC > 4 ? NPROC - 2 : NPROC ))
if [ "$BUILD_JOBS" -gt 8 ]; then
    BUILD_JOBS=8  # Cap to prevent memory issues even on large machines
fi

step "Building with ${BUILD_JOBS} parallel jobs (of ${NPROC} available cores)..."
cmake --build "${BUILD_DIR}" -j "${BUILD_JOBS}" 2>&1 | tee "${WORKSPACE}/build_log.txt"

BUILD_EXIT=$?
if [ $BUILD_EXIT -ne 0 ]; then
    echo ""
    echo -e "${RED}Build failed (exit code $BUILD_EXIT).${NC}"
    echo ""
    echo "Common fixes:"
    echo "  1. OOM during linking? Try: cmake --build ${BUILD_DIR} -j 2"
    echo "  2. CUDA errors? Check CUDA version: nvcc --version"
    echo "  3. See full log: less ${WORKSPACE}/build_log.txt"
    echo "  4. Search for error: grep -i 'error' ${WORKSPACE}/build_log.txt | tail -20"
    exit 1
fi

# --- Verify binary ---
BINARY="${BUILD_DIR}/LichtFeld-Studio"
if [ ! -f "${BINARY}" ]; then
    fail "Build reported success but binary not found at ${BINARY}"
fi

check "Binary exists: $(ls -lh "${BINARY}" | awk '{print $5, $9}')"

# Quick smoke test — just run --help to verify it doesn't segfault
if "${BINARY}" --help > /dev/null 2>&1; then
    check "Binary runs without crashing ✓"
else
    warn "Binary exists but --help returned non-zero. It may still work for training."
fi

pass_stage 6 "Build successful"

# ============================================================================
# STAGE 7: Setup workspace
# ============================================================================
step "Stage 7: Setting up workspace..."
mkdir -p "${DATASETS_DIR}" "${OUTPUT_DIR}"

# Copy training context files
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "${SCRIPT_DIR}/CLAUDE.md" ]; then
    cp "${SCRIPT_DIR}/CLAUDE.md" "${LICHTFELD_DIR}/CLAUDE.md"
fi
mkdir -p "${WORKSPACE}/runpod"
for f in TRAINING_GUIDE.md train.sh download_results.sh README.md CLAUDE.md; do
    [ -f "${SCRIPT_DIR}/${f}" ] && cp "${SCRIPT_DIR}/${f}" "${WORKSPACE}/runpod/${f}"
done
chmod +x "${WORKSPACE}/runpod/"*.sh 2>/dev/null || true

pass_stage 7 "Workspace ready"

# ============================================================================
# SUMMARY
# ============================================================================
echo ""
echo "============================================"
echo -e "${GREEN} All 7 stages passed!${NC}"
echo -e "${GREEN} LichtFeld Studio is ready to train.${NC}"
echo "============================================"
echo ""
echo "  Binary:    ${BINARY}"
echo "  Datasets:  ${DATASETS_DIR}/"
echo "  Output:    ${OUTPUT_DIR}/"
echo ""
echo "  Claude Code: $(command -v claude &>/dev/null && echo 'installed ✓' || echo 'not found')"
echo ""
if nvidia-smi &>/dev/null; then
    echo "  GPU:"
    nvidia-smi --query-gpu=name,memory.total,memory.free --format=csv,noheader
    echo ""
fi
echo "  Next steps:"
echo "    1. Upload dataset:  scp -r ./my_scene root@\$(hostname -I | awk '{print \$1}'):${DATASETS_DIR}/"
echo "    2. Start Claude:    cd ${LICHTFELD_DIR} && claude"
echo "    3. Or train direct: cd ${WORKSPACE}/runpod && ./train.sh --data-path ${DATASETS_DIR}/my_scene"
echo ""
echo "  IMPORTANT: Run training inside tmux to survive SSH disconnects:"
echo "    tmux new -s train"
echo "    ./train.sh --data-path ${DATASETS_DIR}/my_scene --strategy mcmc --iter 30000 --eval --test-every 8"
echo ""
