#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: 06_superc_preprocess.sh [options] <input>

Run SuperC and write preprocessor output for a file or directory.

Options:
  -o, --output-dir DIR    Output directory (default: out/superc_preprocessed)
      --suffix STR        Suffix for output files (default: _superc.c)
      --exts LIST         Comma-separated extensions for directory mode (default: c)
      --java-cmd CMD      Java command (default: JAVA_CMD env or 'java')
      --java-args ARGS    Extra JVM args (default: JAVA_ARGS env)
      --superc-args ARGS  Extra SuperC args (e.g., -E -I /path/include)
      --java-dev-root DIR JAVA_DEV_ROOT for SuperC classes (default: ./superc or env)
  -h, --help              Show this help

Examples:
  ./scripts/06_superc_preprocess.sh superc/fonda/cpp_testsuite/preprocessor/accept.c
  ./scripts/06_superc_preprocess.sh --exts c,h superc/fonda/cpp_testsuite/preprocessor
  ./scripts/06_superc_preprocess.sh --superc-args "-E -I /path/include" some_dir
EOF
}

OUTPUT_DIR="out/superc_preprocessed"
SUFFIX="_superc.c"
EXTS="c"
JAVA_DEV_ROOT="${JAVA_DEV_ROOT:-$(pwd)/superc}"
JAVA_CMD="${JAVA_CMD:-java}"
JAVA_ARGS="${JAVA_ARGS:-}"
SUPERC_ARGS="${SUPERC_ARGS:--E}"
INPUT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o|--output-dir)
      OUTPUT_DIR="$2"; shift 2;;
    --suffix)
      SUFFIX="$2"; shift 2;;
    --exts)
      EXTS="$2"; shift 2;;
    --java-cmd)
      JAVA_CMD="$2"; shift 2;;
    --java-args)
      JAVA_ARGS="$2"; shift 2;;
    --superc-args)
      SUPERC_ARGS="$2"; shift 2;;
    --java-dev-root)
      JAVA_DEV_ROOT="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    --)
      shift; break;;
    -* )
      echo "Unknown option: $1" >&2
      usage; exit 2;;
    * )
      if [[ -z "$INPUT" ]]; then
        INPUT="$1"; shift
      else
        echo "Unexpected argument: $1" >&2
        usage; exit 2
      fi;;
  esac
done

if [[ -z "$INPUT" ]]; then
  usage; exit 2
fi

classpath=(
  "$JAVA_DEV_ROOT/classes"
  "$JAVA_DEV_ROOT/bin/junit.jar"
  "$JAVA_DEV_ROOT/bin/antlr.jar"
  "$JAVA_DEV_ROOT/bin/javabdd.jar"
  "/usr/share/java/org.sat4j.core.jar"
)

CLASSPATH_STR=$(IFS=:; echo "${classpath[*]}")
if [[ -n "${CLASSPATH:-}" ]]; then
  CLASSPATH_STR="$CLASSPATH_STR:$CLASSPATH"
fi

read -r -a java_args <<< "$JAVA_ARGS"
read -r -a superc_args <<< "$SUPERC_ARGS"

run_superc() {
  local input_path="$1"
  local output_path="$2"
  mkdir -p "$(dirname "$output_path")"
  "$JAVA_CMD" "${java_args[@]}" -cp "$CLASSPATH_STR" \
    superc.SuperC -silent "${superc_args[@]}" "$input_path" > "$output_path"
}

relpath() {
  local target="$1"
  local base="$2"
  if command -v realpath >/dev/null 2>&1; then
    realpath --relative-to="$base" "$target"
  else
    python3 - <<'PY' "$base" "$target"
import os
import sys
base, target = sys.argv[1], sys.argv[2]
print(os.path.relpath(target, base))
PY
  fi
}

if [[ -f "$INPUT" ]]; then
  abs_input=$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")
  rel=$(relpath "$abs_input" "$(pwd)")
  if [[ "$rel" == ..* ]]; then
    rel=$(basename "$abs_input")
  fi
  rel_dir=$(dirname "$rel")
  base=$(basename "$rel")
  base="${base%.*}"
  if [[ "$rel_dir" == "." ]]; then
    out_path="$OUTPUT_DIR/$base$SUFFIX"
  else
    out_path="$OUTPUT_DIR/$rel_dir/$base$SUFFIX"
  fi
  run_superc "$INPUT" "$out_path"
  exit 0
fi

if [[ ! -d "$INPUT" ]]; then
  echo "Input not found: $INPUT" >&2
  exit 1
fi

IFS=',' read -r -a exts <<< "$EXTS"
find_expr=()
for ext in "${exts[@]}"; do
  ext="${ext#.}"
  if [[ -n "$ext" ]]; then
    find_expr+=( -name "*.${ext}" -o )
  fi
done

if [[ ${#find_expr[@]} -eq 0 ]]; then
  echo "No valid extensions specified: $EXTS" >&2
  exit 1
fi

unset 'find_expr[${#find_expr[@]}-1]'
root=$(cd "$INPUT" && pwd)

while IFS= read -r -d '' src; do
  rel=$(relpath "$src" "$(pwd)")
  if [[ "$rel" == ..* ]]; then
    rel="${src#"$root"/}"
  fi
  rel_dir=$(dirname "$rel")
  base=$(basename "$src")
  base="${base%.*}"
  if [[ "$rel_dir" == "." ]]; then
    out_path="$OUTPUT_DIR/$base$SUFFIX"
  else
    out_path="$OUTPUT_DIR/$rel_dir/$base$SUFFIX"
  fi
  run_superc "$src" "$out_path"
done < <(find "$root" -type f \( "${find_expr[@]}" \) -print0)
