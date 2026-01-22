#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <input.c> <outdir> [DEFINES_COMMA_SEP]" >&2
  exit 1
fi

INPUT="$1"
OUTDIR="$2"
DEFINES="${3:-}"

TOOLS_BIN="${TOOLS_BIN:-build}"
mkdir -p "$OUTDIR" "$ROOT_DIR/build"

ensure_tools() {
  local tools=(pc_propagation_clang symbolic_lowering_clang emit_ir ir_summary)
  local rebuild=0
  for t in "${tools[@]}"; do
    if [[ ! -x "$ROOT_DIR/$TOOLS_BIN/$t" ]]; then
      rebuild=1
      break
    fi
  done
  if [[ $rebuild -eq 0 ]]; then
    if [[ src/pc_propagation_clang.cpp -nt "$ROOT_DIR/$TOOLS_BIN/pc_propagation_clang" ]]; then
      rebuild=1
    elif [[ src/symbolic_lowering_clang.cpp -nt "$ROOT_DIR/$TOOLS_BIN/symbolic_lowering_clang" ]]; then
      rebuild=1
    elif [[ src/emit_ir.cpp -nt "$ROOT_DIR/$TOOLS_BIN/emit_ir" ]]; then
      rebuild=1
    elif [[ src/ir_summary.cpp -nt "$ROOT_DIR/$TOOLS_BIN/ir_summary" ]]; then
      rebuild=1
    fi
  fi
  if [[ $rebuild -eq 0 ]]; then
    return 0
  fi

  if ! llvm-config --version >/dev/null 2>&1; then
    echo "llvm-config not found. Install LLVM dev packages." >&2
    exit 1
  fi

  CXX=${CXX:-clang++}
  CXXFLAGS=${CXXFLAGS:-"-std=c++17 -O0 -g"}
  LLVM_LIBDIR="$(llvm-config --libdir)"
  LLVM_INCDIR="$(llvm-config --includedir)"

  CLANG_LIBS=""
  if llvm-config --libs clangTooling >/dev/null 2>&1; then
    CLANG_LIBS="$(llvm-config --libs clangTooling clangFrontend clangAST clangBasic clangRewrite clangLex clangParse clangCodeGen)"
  else
    CLANG_CPP_PATH=""
    if ls "$LLVM_LIBDIR"/libclang-cpp.so* >/dev/null 2>&1; then
      CLANG_CPP_PATH="$(ls -1 "$LLVM_LIBDIR"/libclang-cpp.so* | head -n 1)"
    fi
    if [[ -z "$CLANG_CPP_PATH" ]]; then
      echo "Clang libraries not found in llvm-config components or libclang-cpp." >&2
      echo "Install clang dev packages (e.g., libclang-dev/clang-tools) and retry." >&2
      exit 1
    fi
    CLANG_LIBS="$CLANG_CPP_PATH"
  fi

  LLVM_COMMON="$(llvm-config --cxxflags --ldflags --system-libs --libs core) -I$LLVM_INCDIR -L$LLVM_LIBDIR -Wl,-rpath,$LLVM_LIBDIR"
  LLVM_IR="$(llvm-config --cxxflags --ldflags --system-libs --libs core irreader) -I$LLVM_INCDIR -L$LLVM_LIBDIR -Wl,-rpath,$LLVM_LIBDIR"

  echo "[build] pc_propagation_clang"
  $CXX $CXXFLAGS src/pc_propagation_clang.cpp -o build/pc_propagation_clang $LLVM_COMMON $CLANG_LIBS
  echo "[build] symbolic_lowering_clang"
  $CXX $CXXFLAGS src/symbolic_lowering_clang.cpp -o build/symbolic_lowering_clang $LLVM_COMMON $CLANG_LIBS
  echo "[build] emit_ir"
  $CXX $CXXFLAGS src/emit_ir.cpp -o build/emit_ir $LLVM_COMMON $CLANG_LIBS
  echo "[build] ir_summary"
  $CXX $CXXFLAGS src/ir_summary.cpp -o build/ir_summary $LLVM_IR
}

ensure_tools

BASENAME="$(basename "$INPUT" .c)"
BASE_IR="$OUTDIR/${BASENAME}_baseline.ll"
SYM_C="$OUTDIR/${BASENAME}_symbolic.c"
SYM_IR="$OUTDIR/${BASENAME}_symbolic.ll"
PC_C="$OUTDIR/${BASENAME}_pc.c"

DEFINE_ARGS=()
if [[ -n "$DEFINES" ]]; then
  IFS="," read -r -a DEF_ARR <<< "$DEFINES"
  for d in "${DEF_ARR[@]}"; do
    DEFINE_ARGS+=("-D${d}")
  done
fi

RESOURCE_DIR=""
if command -v clang >/dev/null 2>&1; then
  RESOURCE_DIR="$(clang -print-resource-dir 2>/dev/null || true)"
fi
RESOURCE_ARGS=()
if [[ -n "$RESOURCE_DIR" ]]; then
  RESOURCE_ARGS+=("--resource-dir" "$RESOURCE_DIR")
fi

"$ROOT_DIR/$TOOLS_BIN/emit_ir" "$INPUT" --output "$BASE_IR" "${RESOURCE_ARGS[@]}" "${DEFINE_ARGS[@]}"
"$ROOT_DIR/$TOOLS_BIN/pc_propagation_clang" --output "$PC_C" "$INPUT"
"$ROOT_DIR/$TOOLS_BIN/symbolic_lowering_clang" --output "$SYM_C" "$INPUT"
"$ROOT_DIR/$TOOLS_BIN/emit_ir" "$SYM_C" --output "$SYM_IR" "${RESOURCE_ARGS[@]}"

echo "Baseline IR: $BASE_IR"
echo "Symbolic IR: $SYM_IR"
