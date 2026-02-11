#!/bin/bash
set -euo pipefail

# ── Dev build & run script ──────────────────────────
# Usage: ./scripts/run_dev.sh [--asan|--tsan]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# Check third_party
if [ ! -d "${PROJECT_DIR}/third_party/uWebSockets" ]; then
    echo "→ Cloning uWebSockets..."
    mkdir -p "${PROJECT_DIR}/third_party"
    git clone --depth 1 --recurse-submodules \
        https://github.com/uNetworking/uWebSockets.git \
        "${PROJECT_DIR}/third_party/uWebSockets"
fi

# CMake flags
CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Debug"

case "${1:-}" in
    --asan) CMAKE_FLAGS+=" -DENABLE_ASAN=ON" ;;
    --tsan) CMAKE_FLAGS+=" -DENABLE_TSAN=ON" ;;
esac

# Build
echo "→ Configuring..."
cmake -B "${BUILD_DIR}" ${CMAKE_FLAGS} "${PROJECT_DIR}"

echo "→ Building..."
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

# Run with dev defaults
echo "→ Starting game server (dev mode)..."
export PORT="${PORT:-9001}"
export LOG_LEVEL="${LOG_LEVEL:-debug}"
export MAX_ROOMS="${MAX_ROOMS:-10}"
export MAX_PLAYERS_PER_ROOM="${MAX_PLAYERS_PER_ROOM:-4}"

exec "${BUILD_DIR}/gameserver"
