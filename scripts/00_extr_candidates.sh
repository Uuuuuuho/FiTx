#!/bin/bash
# -e: exit on error
# -u: treat unset variables as errors
# -o pipefail: pipeline returns the exit status of the last command in the pipe
set -euo pipefail

LINUX_SRC_DIR="/home/yqc5929/data_workspace/linux"
ROOT_DIR="${1:-drivers}"
OUTPUT_FILE="${2:-out/${ROOT_DIR##*/}_candidates.txt}"
ROOT_DIR="$LINUX_SRC_DIR/$ROOT_DIR"
WINDOW_LINES="${3:-8}"

if [[ ! -d "$ROOT_DIR" ]]; then
  echo "Root directory not found: $ROOT_DIR" >&2
  exit 1
fi

# find all .c and .h files, search for CONFIG_ macros followed within N lines 
# ($WINDOW_LINES) by locking or memory allocation functions, and output 
# the results to OUTPUT_FILE
# -print0 and xargs -0 handle filenames with spaces
# ^#if: matches #if, #ifdef, #ifndef
# (n?def)?: matches optional 'ndef' or 'def'
# [[:space:]]+: matches one or more whitespace characters
# \( and \): group the -name patterns for find
# cfg_line=FNR: store the line number of the CONFIG_ macro (per file)
# cfgline=$0: store the line content of the CONFIG_ macro
find "$ROOT_DIR" -type f \( -name '*.c' -o -name '*.h' \) -print0 \
  | xargs -0 awk -v N="$WINDOW_LINES" '
    BEGIN { cfg=0; cfgline=""; cfg_line=0 }
    FNR==1 { cfg=0; cfgline=""; cfg_line=0 }
    { if ($0 ~ /^#if(n?def)?[[:space:]]+CONFIG_/) { cfg=FNR; cfgline=$0; cfg_line=FNR } }
    { if (cfg && FNR<=cfg+N && $0 ~ /(spin_lock|mutex_lock|read_lock|write_lock|kfree|vfree|kmalloc|kzalloc|alloc|free|refcount|atomic_dec_and_test)/) {
        printf "%s:%d (cfg:%d): %s -> %s\n", FILENAME, FNR, cfg_line, cfgline, $0;
        cfg=0;
      }
    }
  ' \
  | tee "$OUTPUT_FILE"

echo "Saved candidates to: $OUTPUT_FILE"