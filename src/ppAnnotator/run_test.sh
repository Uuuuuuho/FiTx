#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PP_LIB="$SCRIPT_DIR/build/libAnnotator.so"
ROOT_DIR="/home/yqc5929/data_workspace/FiTx/src/ppAnnotator"
TEST_DIR="$SCRIPT_DIR/test"
OUTDIR="$ROOT_DIR/out"
EMIT_LL="${EMIT_LL:-1}"

# Build the plugin if not already built
cmake -B build
make -C build

# Create output directory
mkdir -p "$OUTDIR"
export FITX_ANNOTATOR_DEBUG=1

CLANG_BIN="clang-14"
LEGACY_FLAG="-flegacy-pass-manager"
if "$CLANG_BIN" --version 2>/dev/null | grep -q "clang version 18"; then
    LEGACY_FLAG=""
fi

while IFS= read -r -d '' src; do
    base=$(basename "$src")
    name="${base%.*}"
    out="$OUTDIR/${name}.ll"
    echo "Processing $src -> $out"
    if [ "$EMIT_LL" = "1" ]; then
        work="test/${base}"
        "$CLANG_BIN" $LEGACY_FLAG \
            -Xclang -load -Xclang "$PP_LIB" \
            -Xclang -plugin -Xclang annotator \
            -Xclang -plugin-arg-annotator -Xclang rewrite \
            -fsyntax-only "$work" -I "${name}.h"
        "$CLANG_BIN" $LEGACY_FLAG \
            -S -emit-llvm "$work" -o "$out"
    else
        "$CLANG_BIN" $LEGACY_FLAG \
            -Xclang -load -Xclang "$PP_LIB" \
            -Xclang -add-plugin -Xclang annotator \
            -c "$src" -o /dev/null
    fi
done < <(find "$TEST_DIR" -maxdepth 1 -type f -name '*.c' -print0)
