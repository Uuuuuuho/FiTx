#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"
TOOLS_BIN="${TOOLS_BIN:-build}"

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  echo "Usage: $0 <linux_root> <target_subdir_or_file> [out_dir]" >&2
  echo "  Optional env: EXTRA_CLANG_ARGS_RAW, PCLower_ARGS_RAW" >&2
  exit 0
fi

LINUX_ROOT_ARG="${1:-/home/yqc5929/data_workspace/linux}"
TARGET_SUBDIR="${2:-drivers/net/ethernet/intel/}"
OUTDIR="${3:-$ROOT_DIR/driver_out}"
JOBS="${JOBS:-${4:-4}}"
mkdir -p "$OUTDIR"

"$ROOT_DIR/build.sh"

detect_arch() {
  if [[ -n "${KERNEL_ARCH:-}" ]]; then
    echo "$KERNEL_ARCH"
    return 0
  fi
  if [[ -n "${ARCH:-}" ]]; then
    echo "$ARCH"
    return 0
  fi
  local uname_arch
  uname_arch="$(uname -m)"
  case "$uname_arch" in
    x86_64|i686|i386) echo "x86" ;;
    aarch64) echo "arm64" ;;
    arm*) echo "arm" ;;
    riscv64) echo "riscv" ;;
    *) echo "$uname_arch" ;;
  esac
}

KERNEL_ARGS=()
LINUX_ROOT="${LINUX_ROOT_ARG%/}"
if [[ -z "$LINUX_ROOT" ]]; then
  echo "LINUX_ROOT is required (arg1). Example: $0 <linux_root> <target_subdir> [out_dir]" >&2
  exit 1
fi

TARGET_FILE=""
if [[ "$TARGET_SUBDIR" = /* ]]; then
  TARGET_DIR="$TARGET_SUBDIR"
else
  TARGET_DIR="$LINUX_ROOT/$TARGET_SUBDIR"
fi
TARGET_DIR="${TARGET_DIR%/}"
if [[ -f "$TARGET_DIR" ]]; then
  TARGET_FILE="$TARGET_DIR"
  TARGET_DIR="$(dirname "$TARGET_FILE")"
fi
if [[ ! -d "$TARGET_DIR" ]]; then
  echo "Target directory not found: $TARGET_DIR" >&2
  exit 1
fi

TARGET_OUT_REL="${TARGET_SUBDIR%/}"
if [[ -n "$TARGET_FILE" ]]; then
  TARGET_OUT_REL="$(dirname "$TARGET_OUT_REL")"
fi
if [[ "$TARGET_OUT_REL" = /* ]]; then
  if [[ "$TARGET_OUT_REL" == "$LINUX_ROOT"/* ]]; then
    TARGET_OUT_REL="${TARGET_OUT_REL#"$LINUX_ROOT"/}"
  else
    TARGET_OUT_REL="$(basename "$TARGET_OUT_REL")"
  fi
fi

AUTO_INCLUDE_ARGS=()
AUTO_INCLUDE_ARGS+=("-I$TARGET_DIR")
if [[ -d "$TARGET_DIR/include" ]]; then
  AUTO_INCLUDE_ARGS+=("-I$TARGET_DIR/include")
fi

if [[ -n "$LINUX_ROOT" ]]; then
  KARCH="$(detect_arch)"
  if [[ -d "$LINUX_ROOT/arch/$KARCH/include" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/arch/$KARCH/include")
  fi
  if [[ -d "$LINUX_ROOT/arch/$KARCH/include/generated" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/arch/$KARCH/include/generated")
  fi
  AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/include")
  if [[ -d "$LINUX_ROOT/include/generated" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/include/generated")
  fi
  if [[ -d "$LINUX_ROOT/include/uapi" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/include/uapi")
  fi
  if [[ -d "$LINUX_ROOT/arch/$KARCH/include/uapi" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/arch/$KARCH/include/uapi")
  fi
  if [[ -d "$LINUX_ROOT/include/generated/uapi" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/include/generated/uapi")
  fi
  if [[ -d "$LINUX_ROOT/arch/$KARCH/include/generated/uapi" ]]; then
    AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/arch/$KARCH/include/generated/uapi")
  fi

  if [[ "$TARGET_DIR" == *"/drivers/video/backlight"* ]]; then
    if [[ -d "$LINUX_ROOT/drivers/video" ]]; then
      AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/drivers/video")
    fi
    if [[ -d "$LINUX_ROOT/drivers/video/backlight" ]]; then
      AUTO_INCLUDE_ARGS+=("-I$LINUX_ROOT/drivers/video/backlight")
    fi
  fi

  KERNEL_ARGS+=("-D__KERNEL__")
  KERNEL_ARGS+=("-DMODULE")
  KERNEL_ARGS+=("-DKBUILD_MODNAME=\"pclower\"")
  KERNEL_ARGS+=("-DKBUILD_BASENAME=\"pclower\"")
  KERNEL_ARGS+=("-DKBUILD_MODFILE=\"pclower\"")
  if [[ -z "${DISABLE_FENTRY_DEFINE:-}" ]]; then
    KERNEL_ARGS+=("-DCC_USING_FENTRY")
  fi
  if [[ -f "$LINUX_ROOT/include/linux/kconfig.h" ]]; then
    KERNEL_ARGS+=("-include" "$LINUX_ROOT/include/linux/kconfig.h")
  fi
  if [[ -f "$LINUX_ROOT/include/generated/autoconf.h" ]]; then
    KERNEL_ARGS+=("-include" "$LINUX_ROOT/include/generated/autoconf.h")
  fi
fi

PCLower_ARGS=()
if [[ -n "${PCLower_ARGS_RAW:-}" ]]; then
  read -r -a PCLower_ARGS <<< "$PCLower_ARGS_RAW"
fi

EXTRA_CLANG_ARGS=()
if [[ -n "${EXTRA_CLANG_ARGS_RAW:-}" ]]; then
  read -r -a EXTRA_CLANG_ARGS <<< "$EXTRA_CLANG_ARGS_RAW"
fi
HAS_STD=false
for arg in "${EXTRA_CLANG_ARGS[@]}"; do
  if [[ "$arg" == -std=* ]]; then
    HAS_STD=true
    break
  fi
done
if [[ "$HAS_STD" == false ]]; then
  EXTRA_CLANG_ARGS+=("-std=gnu89")
fi
EXTRA_CLANG_ARGS+=("-Wno-implicit-function-declaration")

PLUGIN_PATH="$ROOT_DIR/$TOOLS_BIN/libPcLower.so"

if [[ -n "$TARGET_FILE" ]]; then
  SOURCES=("$TARGET_FILE")
else
  mapfile -t SOURCES < <(find "$TARGET_DIR" -type f -name '*.c')
fi

if [[ ${#SOURCES[@]} -eq 0 ]]; then
  echo "No .c files found under $TARGET_DIR" >&2
  exit 1
fi

if [[ -z "${JOBS:-}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS=1
  fi
fi
if [[ "$JOBS" -lt 1 ]]; then
  JOBS=1
fi

run_one() {
  local input="$1"
  local rel_path rel_dir base_name file_dir ex_out sym_c sym_ir

  rel_path="${input#$TARGET_DIR/}"
  if [[ -n "$TARGET_FILE" ]]; then
    rel_dir="."
  else
    rel_dir="$(dirname "$rel_path")"
  fi
  base_name="$(basename "$input" .c)"
  file_dir="$(dirname "$input")"
  local file_include_args=("${AUTO_INCLUDE_ARGS[@]}" "-I$file_dir")

  ex_out="$OUTDIR/$TARGET_OUT_REL/$rel_dir"
  mkdir -p "$ex_out"

  sym_c="$ex_out/${base_name}_symbolic.c"
  sym_ir="$ex_out/${base_name}_symbolic.ll"

  if [[ -s "$sym_ir" ]]; then
    echo "Skip (done): $sym_ir"
    return 0
  fi

  if ! clang -fsyntax-only "${file_include_args[@]}" "${KERNEL_ARGS[@]}" "${EXTRA_CLANG_ARGS[@]}" \
    "$input" >/dev/null; then
    echo "Skip (headers): $input" >&2
    return 0
  fi

  if ! clang -fsyntax-only "${file_include_args[@]}" "${KERNEL_ARGS[@]}" "${EXTRA_CLANG_ARGS[@]}" \
    -Xclang -load -Xclang "$PLUGIN_PATH" \
    -Xclang -plugin -Xclang pclower \
    -Xclang -plugin-arg-pclower -Xclang "lowered-out=$sym_c" \
    ${PCLower_ARGS[@]/#/-Xclang -plugin-arg-pclower -Xclang } \
    "$input" >/dev/null; then
    echo "Skip (plugin): $input" >&2
    return 0
  fi

  if ! clang -S -emit-llvm -O0 -g "${file_include_args[@]}" "${KERNEL_ARGS[@]}" "${EXTRA_CLANG_ARGS[@]}" "$sym_c" -o "$sym_ir"; then
    echo "Skip IR: $sym_c" >&2
    return 0
  fi

  echo "Symbolic IR: $sym_ir"
}

for INPUT in "${SOURCES[@]}"; do
  while (( $(jobs -rp | wc -l) >= JOBS )); do
    sleep 0.1
  done
  run_one "$INPUT" &
done
wait

echo "All transforms completed."
