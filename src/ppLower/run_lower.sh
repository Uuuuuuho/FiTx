#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_BIN="${TOOLS_BIN:-build}"
BUILD_PLUGIN=true

INPUT=""
OUTPUT=""
PC_OUT=""

CLANG_ARGS=()
DEFINE_ARGS=()
INCLUDE_ARGS=()
PCLower_ARGS=()

print_usage() {
  cat <<'USAGE'
Usage: run_lower.sh -i <input.c> -o <lowered.c> [options]

Options:
  -i, --input <file>         Input C source file
  -o, --output <file>        Output lowered C file
  --pc <file>                Output PC file (default: <output>_pc.c)
  -D <macro>                 Define macro (repeatable)
  -I <dir>                   Add include directory (repeatable)
  --plugin-arg <arg>         Add pclower plugin arg (repeatable)
  --no-build                 Skip build step
  --                         Pass remaining args to clang
  -h, --help                 Show this help

Examples:
  ./run_lower.sh -i test/example1_alloc_free.c -o out/example1_symbolic.c \
    -D CONFIG_USB -I test
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -i|--input)
      INPUT="$2"
      shift 2
      ;;
    -o|--output)
      OUTPUT="$2"
      shift 2
      ;;
    --pc)
      PC_OUT="$2"
      shift 2
      ;;
    -D)
      DEFINE_ARGS+=("-D$2")
      shift 2
      ;;
    -I)
      INCLUDE_ARGS+=("-I$2")
      shift 2
      ;;
    --plugin-arg)
      PCLower_ARGS+=("$2")
      shift 2
      ;;
    --no-build)
      BUILD_PLUGIN=false
      shift
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    --)
      shift
      CLANG_ARGS+=("$@");
      break
      ;;
    *)
      echo "Unknown option: $1" >&2
      print_usage >&2
      exit 1
      ;;
  esac
 done

if [[ -z "$INPUT" || -z "$OUTPUT" ]]; then
  print_usage >&2
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Input file not found: $INPUT" >&2
  exit 1
fi

OUTPUT_DIR="$(dirname "$OUTPUT")"
mkdir -p "$OUTPUT_DIR"

if [[ -z "$PC_OUT" ]]; then
  PC_OUT="${OUTPUT%.c}_pc.c"
fi

if $BUILD_PLUGIN; then
  "$ROOT_DIR/build.sh"
fi

PLUGIN_PATH="$ROOT_DIR/$TOOLS_BIN/pclower_plugin.so"
if [[ ! -f "$PLUGIN_PATH" ]]; then
  echo "Plugin not found: $PLUGIN_PATH" >&2
  exit 1
fi

INPUT_DIR="$(cd "$(dirname "$INPUT")" && pwd)"
INCLUDE_ARGS+=("-I$INPUT_DIR")

clang -fsyntax-only "${DEFINE_ARGS[@]}" "${INCLUDE_ARGS[@]}" \
  -Xclang -load -Xclang "$PLUGIN_PATH" \
  -Xclang -plugin -Xclang pclower \
  -Xclang -plugin-arg-pclower -Xclang "pc-out=$PC_OUT" \
  -Xclang -plugin-arg-pclower -Xclang "lowered-out=$OUTPUT" \
  ${PCLower_ARGS[@]/#/-Xclang -plugin-arg-pclower -Xclang } \
  "${CLANG_ARGS[@]}" \
  "$INPUT" >/dev/null

echo "Lowered C: $OUTPUT"
echo "PC file:   $PC_OUT"
