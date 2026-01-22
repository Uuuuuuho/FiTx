#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"
TOOLS_BIN="${TOOLS_BIN:-build}"

OUTDIR="$ROOT_DIR/out"
mkdir -p "$OUTDIR"

declare -A DEFINES
DEFINES[example1_alloc_free]="CONFIG_USB"
DEFINES[example2_branch]="CONFIG_NET"
DEFINES[example3_struct]="CONFIG_A,CONFIG_B"
DEFINES[example4_function]="CONFIG_X"
DEFINES[example5_macro]="CONFIG_DEBUG"

examples=(
  example1_alloc_free
  example2_branch
  example3_struct
  example4_function
  example5_macro
)

failures=0

for ex in "${examples[@]}"; do
  echo "==== $ex ===="
  INPUT="$ROOT_DIR/test/${ex}.c"
  EX_OUT="$OUTDIR/$ex"
  mkdir -p "$EX_OUT"

  DEFINE_ARGS=()
  if [[ -n "${DEFINES[$ex]}" ]]; then
    IFS="," read -r -a DEF_ARR <<< "${DEFINES[$ex]}"
    for d in "${DEF_ARR[@]}"; do
      DEFINE_ARGS+=("-D${d}")
    done
  fi

  TOOLS_BIN="$TOOLS_BIN" "$ROOT_DIR/build.sh" "$INPUT" "$EX_OUT" "${DEFINES[$ex]}"

  clang -O0 -g -o "$EX_OUT/${ex}_baseline" "$INPUT" "${DEFINE_ARGS[@]}"
  clang -O0 -g -o "$EX_OUT/${ex}_symbolic" "$EX_OUT/${ex}_symbolic.c"

  "$EX_OUT/${ex}_baseline" >/dev/null
  "$EX_OUT/${ex}_symbolic" >/dev/null

  echo "-- IR summary (baseline)"
  "$ROOT_DIR/$TOOLS_BIN/ir_summary" "$EX_OUT/${ex}_baseline.ll"
  echo "-- IR summary (symbolic)"
  "$ROOT_DIR/$TOOLS_BIN/ir_summary" "$EX_OUT/${ex}_symbolic.ll"

  echo "-- IR diff (baseline vs symbolic)"
  diff -u "$EX_OUT/${ex}_baseline.ll" "$EX_OUT/${ex}_symbolic.ll" | head -n 120 || true

  echo
 done

echo "All tests completed."
