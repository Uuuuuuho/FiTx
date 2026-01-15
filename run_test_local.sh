#!/bin/bash

# Run FiTx tests locally on host Linux without Docker
# This script sets up the environment and runs the analyze.py script

set -e  # Exit on error

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Set environment variables for local paths
export FITX_ROOT="${SCRIPT_DIR}"
export LINUX_ROOT="${LINUX_ROOT:-/home/yqc5929/workspace/linux}"

# Ensure log directory exists
mkdir -p "${SCRIPT_DIR}/log"

# Check if required tools are available
if ! command -v clang-14 &> /dev/null; then
    echo "Error: clang-14 not found. Please install it first."
    exit 1
fi

if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found. Please install it first."
    exit 1
fi

# Check if detector library exists
DETECTOR_LIB="${SCRIPT_DIR}/build/detector/all_detector/libAllDetectorMod.so"
if [ ! -f "${DETECTOR_LIB}" ]; then
    echo "Error: Detector library not found at ${DETECTOR_LIB}"
    echo "Please build the project first using ./build_local.sh"
    exit 1
fi

# Check Python dependencies
if ! python3 -c "import click" 2>/dev/null; then
    echo "Installing Python dependencies..."
    pip3 install -r "${SCRIPT_DIR}/scripts/requirements.txt" --user
fi

# Run the test
echo "Running FiTx tests..."
echo "FITX_ROOT: ${FITX_ROOT}"
echo "LINUX_ROOT: ${LINUX_ROOT}"
echo ""

# Create a temporary clang symlink in a temporary bin directory
TMP_BIN=$(mktemp -d)
CLANG_BIN="$(command -v clang-14 || true)"
if [ -z "${CLANG_BIN}" ]; then
    echo "Error: clang-14 not found in PATH" >&2
    exit 1
fi
ln -s "${CLANG_BIN}" "${TMP_BIN}/clang"
export PATH="${TMP_BIN}:$(dirname "${CLANG_BIN}"):${PATH}"

# Force tooling to use clang-14 explicitly
export CC="${CLANG_BIN}"
export HOSTCC="${CLANG_BIN}"
export CLANG="${CLANG_BIN}"

# Cleanup function
cleanup() {
    rm -rf "${TMP_BIN}"
}
trap cleanup EXIT

# Run the test
cd "${SCRIPT_DIR}"
python3 scripts/analyze.py test tests/

echo ""
echo "Test completed!"

# Run the analysis for Linux kernel
# Use 'yes' to automatically answer configuration questions with default (typically 'n')
# or redirect from /dev/null to use defaults without interaction
echo ""
echo "Running Linux kernel analysis..."
# yes '' 2>/dev/null | python3 scripts/analyze.py linux || python3 scripts/analyze.py linux < /dev/null
python3 scripts/analyze.py linux