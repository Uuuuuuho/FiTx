#!/bin/bash

# Build FiTx locally on host Linux
# This script replicates the Docker build steps for local execution

set -e  # Exit on error

# Configuration
PROJECT_PATH="${PROJECT_PATH:-$(pwd)}"
NPROC="${NPROC:-$(nproc)}"
LLVM_DIR="${LLVM_DIR:-/usr/lib/llvm-14/lib/cmake/llvm}"

echo "Building FiTx..."
echo "Project path: ${PROJECT_PATH}"
echo "Number of processors: ${NPROC}"
echo "LLVM directory: ${LLVM_DIR}"

# Ensure python3 is available (needed for venv/pip)
if ! command -v python3 >/dev/null 2>&1; then
	echo "Error: python3 not found in PATH" >&2
	exit 1
fi

# Export LLVM_DIR for cmake
export LLVM_DIR

# Clean previous build directory
rm -rf "${PROJECT_PATH}/build"

# Create build directory if it doesn't exist
mkdir -p "${PROJECT_PATH}/build"

# Navigate to build directory
cd "${PROJECT_PATH}/build"

# Run cmake
echo "Running cmake..."
cmake -S "${PROJECT_PATH}/src"

# Build with make
echo "Building with make..."
make -j "${NPROC}"

# Install Python requirements inside a local virtualenv to avoid system pip issues (PEP 668)
VENV_PATH="${VENV_PATH:-${PROJECT_PATH}/.venv}"
echo "Using virtualenv at ${VENV_PATH}"

if [ ! -d "${VENV_PATH}" ]; then
	echo "Creating virtualenv..."
	python3 -m venv "${VENV_PATH}"
fi

# Activate venv
# shellcheck disable=SC1090
source "${VENV_PATH}/bin/activate"

echo "Upgrading pip in virtualenv..."
python -m pip install --upgrade pip

echo "Installing Python requirements..."
python -m pip install -r "${PROJECT_PATH}/scripts/requirements.txt"

echo "Build completed successfully!"
