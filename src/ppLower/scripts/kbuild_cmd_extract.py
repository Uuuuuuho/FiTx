#!/usr/bin/env python3
import argparse
import os
import re
import shlex
import sys


COMMAND_RE = re.compile(r"\bclang\b.*?\s-c\s+\S+\.c\b")


def extract_commands(lines):
    for line in lines:
        match = COMMAND_RE.search(line)
        if not match:
            continue
        cmd = match.group(0)
        if cmd.startswith("clang "):
            yield cmd


def normalize_command(cmd, source_root):
    parts = shlex.split(cmd)
    if not parts:
        return None
    compiler = "clang"
    src = None
    for idx, part in enumerate(parts):
        if part.endswith(".c"):
            src = os.path.realpath(os.path.join(source_root, part) if not os.path.isabs(part) else part)
    if src is None:
        return None
    filtered = []
    skip_next = False
    for part in parts[1:]:
        if skip_next:
            skip_next = False
            continue
        if part in {"-c"}:
            continue
        if part in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if part in {"-MD", "-MMD"}:
            continue
        if part.startswith("-o"):
            continue
        if part.startswith("-O") or part == "-g":
            continue
        if os.path.realpath(os.path.join(source_root, part) if not os.path.isabs(part) else part) == src:
            continue
        filtered.append(part)
    return src, compiler, filtered


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--target-dir", required=True)
    args = parser.parse_args()

    source_root = os.path.realpath(args.source_root)
    target_dir = os.path.realpath(args.target_dir)

    if not os.path.isdir(target_dir):
        print(f"Target directory not found: {target_dir}", file=sys.stderr)
        return 2

    lines = sys.stdin.read().splitlines()
    for cmd in extract_commands(lines):
        result = normalize_command(cmd, source_root)
        if result is None:
            continue
        src, compiler, filtered = result
        if not src.startswith(target_dir + os.sep):
            continue
        print(src + "\t" + shlex.join(filtered))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
