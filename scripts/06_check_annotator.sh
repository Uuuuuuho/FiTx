#!/usr/bin/env bash
set -euo pipefail

CLANG_BIN="${CLANG_BIN:-clang-14}"
PRJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="${PLUGIN_PATH:-${PRJ_DIR}/build/pp/libAnnotator.so}"
OUT_DIR="${OUT_DIR:-out/annotator_check}"
EXTRA_FLAGS=()
LINUX_ROOT=""
ARCH_NAME=""

usage() {
  cat <<'USAGE'
Usage: check_annotator.sh [options] <file> [file...]

Options:
  --clang PATH       clang binary (default: clang-14)
  --plugin PATH      libAnnotator.so path (default: build/pp/libAnnotator.so)
  --out-dir DIR      output directory (default: out/annotator_check)
  --flags "..."      extra clang flags (quoted)
  --linux-root DIR   Linux kernel tree (adds common include flags)
  --arch ARCH        Kernel arch for includes (default: x86 for x86_64)
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clang)
      CLANG_BIN="$2"; shift 2 ;;
    --plugin)
      PLUGIN_PATH="$2"; shift 2 ;;
    --out-dir)
      OUT_DIR="$2"; shift 2 ;;
    --flags)
      read -r -a extra <<< "$2"
      EXTRA_FLAGS+=("${extra[@]}")
      shift 2 ;;
    --linux-root)
      LINUX_ROOT="$2"; shift 2 ;;
    --arch)
      ARCH_NAME="$2"; shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    --)
      shift; break ;;
    -*)
      echo "Unknown option: $1" >&2; usage; exit 1 ;;
    *)
      break ;;
  esac
done

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

mkdir -p "$OUT_DIR"

if [[ -n "$LINUX_ROOT" ]]; then
  if [[ -z "$ARCH_NAME" ]]; then
    case "$(uname -m)" in
      x86_64) ARCH_NAME="x86" ;;
      aarch64) ARCH_NAME="arm64" ;;
      armv7*) ARCH_NAME="arm" ;;
      *) ARCH_NAME="$(uname -m)" ;;
    esac
  fi
  EXTRA_FLAGS+=(
    -D__KERNEL__
    -D__LINUX__
    -include "$LINUX_ROOT/include/linux/kconfig.h"
    -I "$LINUX_ROOT/include"
    -I "$LINUX_ROOT/arch/$ARCH_NAME/include"
    -I "$LINUX_ROOT/arch/$ARCH_NAME/include/generated"
    -I "$LINUX_ROOT/include/generated"
    -I "$LINUX_ROOT/include/uapi"
    -I "$LINUX_ROOT/arch/$ARCH_NAME/include/uapi"
    -I "$LINUX_ROOT/arch/$ARCH_NAME/include/generated/uapi"
  )
fi

if command -v rg >/dev/null 2>&1; then
  GREP_BIN="rg"
  GREP_FLAGS="-n"
else
  GREP_BIN="grep"
  GREP_FLAGS="-n"
fi

idx=0
for src in "$@"; do
  if [[ ! -f "$src" ]]; then
    echo "Missing file: $src" >&2
    continue
  fi
  base=$(basename "$src")
  base_noext="${base%.*}"
  work="${OUT_DIR}/${idx}_${base}"
  llout="${work}.ll"
  cp "$src" "$work"

  local_flags=("${EXTRA_FLAGS[@]}")
  local_flags+=("-I" "$(dirname "$src")")
  local_flags+=("-DKBUILD_MODNAME=\"${base_noext}\"")
  local_flags+=("-DKBUILD_BASENAME=\"${base_noext}\"")

  # Pass 1: run annotator to rewrite the copied file (adds annotate stubs).
  ${CLANG_BIN} -fsyntax-only "$work" \
    -Xclang -load -Xclang "$PLUGIN_PATH" \
    -Xclang -plugin -Xclang annotator \
    -Xclang -plugin-arg-annotator -Xclang rewrite \
    "${local_flags[@]}"

  # Pass 2: emit LLVM IR from the rewritten file.
  ${CLANG_BIN} -S -emit-llvm "$work" -o "$llout" "${local_flags[@]}"

  echo "[check] $llout"
  $GREP_BIN $GREP_FLAGS "inactive_block" "$llout" || true
  $GREP_BIN $GREP_FLAGS "llvm.global.annotations" "$llout" || true
  idx=$((idx + 1))
done
