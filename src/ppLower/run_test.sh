#!/usr/bin/env bash
# This script runs a series of tests for the PCLower tool, compiling example C programs,
# generating their baseline and symbolically lowered LLVM IR, and executing them.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"
TOOLS_BIN="${TOOLS_BIN:-build}"

OUTDIR="$ROOT_DIR/out"
mkdir -p "$OUTDIR"

# Use CLANG_BIN so we can force clang-14 in the environment
CLANG_BIN="${CLANG_BIN:-clang-14}"

"$ROOT_DIR/build.sh"

PCLower_ARGS=()
if [[ -n "${PCLower_ARGS_RAW:-}" ]]; then
  read -r -a PCLower_ARGS <<< "$PCLower_ARGS_RAW"
fi

declare -A DEFINES
DEFINES[example1_alloc_free]="CONFIG_USB"
DEFINES[example2_branch]="CONFIG_NET"
DEFINES[example3_struct]="CONFIG_A,CONFIG_B"
DEFINES[example4_function]="CONFIG_X"
DEFINES[example5_macro]="CONFIG_DEBUG"
DEFINES[example6_multi]="CONFIG_SYNC"
DEFINES[example7_macro_fn]=""
DEFINES[example8_nested_macro]="CONFIG_A"
DEFINES[example9_multi_fn]="CONFIG_A"

declare -A EXAMPLE_SOURCES
EXAMPLE_SOURCES[example6_multi]="example6_multi_main.c example6_multi_lib.c"

examples=(
  example1_alloc_free
  example2_branch
  example3_struct
  example4_function
  example5_macro
  example6_multi
  example7_macro_fn
  example8_nested_macro
  example9_multi_fn
)

ALLOW_NONZERO_RUNS=(
  example7_macro_fn
  example9_multi_fn
)

failures=0

for ex in "${examples[@]}"; do
  echo "==== $ex ===="
  allow_nonzero=false
  for ok in "${ALLOW_NONZERO_RUNS[@]}"; do
    if [[ "$ok" == "$ex" ]]; then
      allow_nonzero=true
      break
    fi
  done
  SOURCES=()
  if [[ -n "${EXAMPLE_SOURCES[$ex]:-}" ]]; then
    for src in ${EXAMPLE_SOURCES[$ex]}; do
      SOURCES+=("$ROOT_DIR/test/$src")
    done
  else
    SOURCES+=("$ROOT_DIR/test/${ex}.c")
  fi
  EX_OUT="$OUTDIR/$ex"
  mkdir -p "$EX_OUT"

  SYM_SOURCES=()
  BASE_IRS=()
  SYM_IRS=()

  DEFINE_ARGS=()
  if [[ -n "${DEFINES[$ex]}" ]]; then
    IFS="," read -r -a DEF_ARR <<< "${DEFINES[$ex]}"
    for d in "${DEF_ARR[@]}"; do
      DEFINE_ARGS+=("-D${d}")
    done
  fi
  INCLUDE_ARGS=("-I$ROOT_DIR/test")

  PLUGIN_PATH="$ROOT_DIR/$TOOLS_BIN/libPcLower.so"

  for INPUT in "${SOURCES[@]}"; do
    BASENAME="$(basename "$INPUT" .c)"
    BASE_IR="$EX_OUT/${BASENAME}_baseline.ll"
    SYM_C="$EX_OUT/${BASENAME}_symbolic.c"
    SYM_IR="$EX_OUT/${BASENAME}_symbolic.ll"
    PC_C="$EX_OUT/${BASENAME}_pc.c"

    "$CLANG_BIN" -fsyntax-only "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}" \
      -Xclang -load -Xclang "$PLUGIN_PATH" \
      -Xclang -plugin -Xclang pclower \
      -Xclang -plugin-arg-pclower -Xclang "pc-out=$PC_C" \
      -Xclang -plugin-arg-pclower -Xclang "lowered-out=$SYM_C" \
      -Xclang -plugin-arg-pclower -Xclang "lowered-overwrite" \
      ${PCLower_ARGS[@]/#/-Xclang -plugin-arg-pclower -Xclang } \
      "$INPUT" >/dev/null

    "$CLANG_BIN" -S -emit-llvm -O0 -g "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}" "$INPUT" -o "$BASE_IR"
    "$CLANG_BIN" -S -emit-llvm -O0 -g "${INCLUDE_ARGS[@]}" "$SYM_C" -o "$SYM_IR"

    SYM_SOURCES+=("$SYM_C")
    BASE_IRS+=("$BASE_IR")
    SYM_IRS+=("$SYM_IR")

    echo "Baseline IR: $BASE_IR"
    echo "Symbolic IR: $SYM_IR"
  done

  "$CLANG_BIN" -O0 -g -o "$EX_OUT/${ex}_baseline" "${SOURCES[@]}" "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}"
  "$CLANG_BIN" -O0 -g -o "$EX_OUT/${ex}_symbolic" "${SYM_SOURCES[@]}" "${INCLUDE_ARGS[@]}"

  if $allow_nonzero; then
    "$EX_OUT/${ex}_baseline" >/dev/null || true
    "$EX_OUT/${ex}_symbolic" >/dev/null || true
  else
    "$EX_OUT/${ex}_baseline" >/dev/null
    "$EX_OUT/${ex}_symbolic" >/dev/null
  fi

  for idx in "${!BASE_IRS[@]}"; do
    echo "-- IR summary (baseline)"
    "$ROOT_DIR/$TOOLS_BIN/ir_summary" "${BASE_IRS[$idx]}"
    echo "-- IR summary (symbolic)"
    "$ROOT_DIR/$TOOLS_BIN/ir_summary" "${SYM_IRS[$idx]}"

    echo "-- IR diff (baseline vs symbolic)"
    diff -u "${BASE_IRS[$idx]}" "${SYM_IRS[$idx]}" | head -n 120 || true
  done

  echo
 done

echo "All tests completed."
