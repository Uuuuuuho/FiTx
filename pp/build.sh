#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

TOOLS_BIN="${TOOLS_BIN:-build}"
mkdir -p "$ROOT_DIR/$TOOLS_BIN"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Install CMake to build the tools." >&2
  exit 1
fi

if [[ ! -f "$ROOT_DIR/$TOOLS_BIN/CMakeCache.txt" ]]; then
  echo "[cmake] configure"
  cmake -S "$ROOT_DIR" -B "$ROOT_DIR/$TOOLS_BIN"
fi
echo "[cmake] build"
cmake --build "$ROOT_DIR/$TOOLS_BIN"