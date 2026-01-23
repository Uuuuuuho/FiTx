#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"
TOOLS_BIN="${TOOLS_BIN:-build}"

OUTDIR="$ROOT_DIR/out"
mkdir -p "$OUTDIR"

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

declare -A EXAMPLE_SOURCES
EXAMPLE_SOURCES[example6_multi]="example6_multi_main.c example6_multi_lib.c"

examples=(
  example1_alloc_free
  example2_branch
  example3_struct
  example4_function
  example5_macro
  example6_multi
)

failures=0

for ex in "${examples[@]}"; do
  echo "==== $ex ===="
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

  PLUGIN_PATH="$ROOT_DIR/$TOOLS_BIN/pclower_plugin.so"

  for INPUT in "${SOURCES[@]}"; do
    BASENAME="$(basename "$INPUT" .c)"
    BASE_IR="$EX_OUT/${BASENAME}_baseline.ll"
    SYM_C="$EX_OUT/${BASENAME}_symbolic.c"
    SYM_IR="$EX_OUT/${BASENAME}_symbolic.ll"
    PC_C="$EX_OUT/${BASENAME}_pc.c"

    clang -fsyntax-only "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}" \
      -Xclang -load -Xclang "$PLUGIN_PATH" \
      -Xclang -plugin -Xclang pclower \
      -Xclang -plugin-arg-pclower -Xclang "pc-out=$PC_C" \
      -Xclang -plugin-arg-pclower -Xclang "lowered-out=$SYM_C" \
      ${PCLower_ARGS[@]/#/-Xclang -plugin-arg-pclower -Xclang } \
      "$INPUT" >/dev/null

    clang -S -emit-llvm -O0 -g "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}" "$INPUT" -o "$BASE_IR"
    clang -S -emit-llvm -O0 -g "${INCLUDE_ARGS[@]}" "$SYM_C" -o "$SYM_IR"

    SYM_SOURCES+=("$SYM_C")
    BASE_IRS+=("$BASE_IR")
    SYM_IRS+=("$SYM_IR")

    echo "Baseline IR: $BASE_IR"
    echo "Symbolic IR: $SYM_IR"
  done

  clang -O0 -g -o "$EX_OUT/${ex}_baseline" "${SOURCES[@]}" "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}"
  clang -O0 -g -o "$EX_OUT/${ex}_symbolic" "${SYM_SOURCES[@]}" "${INCLUDE_ARGS[@]}"

  "$EX_OUT/${ex}_baseline" >/dev/null
  "$EX_OUT/${ex}_symbolic" >/dev/null

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
