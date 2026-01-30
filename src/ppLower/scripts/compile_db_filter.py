#!/usr/bin/env python3
import json
import os
import shlex
import sys


def normalize_command(entry):
    if "arguments" in entry:
        return entry["arguments"]
    return shlex.split(entry["command"])


def filter_args(args, source_file):
    filtered = []
    skip_next = False
    src_real = os.path.realpath(source_file)
    for arg in args[1:]:
        if skip_next:
            skip_next = False
            continue
        if arg in {"-c"}:
            continue
        if arg in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if arg in {"-MD", "-MMD"}:
            continue
        if arg.startswith("-o"):
            continue
        if arg.startswith("-O") or arg == "-g":
            continue
        if os.path.realpath(arg) == src_real:
            continue
        filtered.append(arg)
    return filtered


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: compile_db_filter.py <compile_commands.json> <target_dir>", file=sys.stderr)
        return 2
    db_path = sys.argv[1]
    target_dir = os.path.realpath(sys.argv[2])

    with open(db_path, "r", encoding="utf-8") as f:
        db = json.load(f)

    for entry in db:
        src = os.path.realpath(entry.get("file", ""))
        if not src.startswith(target_dir + os.sep):
            continue
        cmd = normalize_command(entry)
        if not cmd:
            continue
        cmd[0] = "clang"
        args = filter_args(cmd, src)
        print(src + "\t" + shlex.join(args))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
