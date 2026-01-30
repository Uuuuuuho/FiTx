#!/usr/bin/env python3
import argparse
import os
import re
from collections import Counter, defaultdict


LINE_RE = re.compile(r"^(?P<path>.*?):\d+ \(cfg:\d+\):")


def parse_lines(lines):
    for line in lines:
        line = line.rstrip("\n")
        if not line:
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        yield m.group("path")


def relpath(path, linux_root):
    if linux_root and path.startswith(linux_root.rstrip("/") + "/"):
        return path[len(linux_root.rstrip("/")) + 1 :]
    return path


def split_subsystem(rel_path):
    # Expect drivers/<subsystem>/...
    parts = rel_path.split("/")
    if len(parts) >= 2 and parts[0] == "drivers":
        return parts[1]
    return parts[0] if parts else ""


def split_device(rel_path):
    # Device is the immediate parent directory name
    parent = os.path.dirname(rel_path)
    return os.path.basename(parent)


def group_key(rel_path, group):
    if group == "file":
        return rel_path
    if group == "device":
        return split_device(rel_path)
    if group == "dir":
        return os.path.dirname(rel_path)
    if group == "subsystem":
        return split_subsystem(rel_path)
    if group == "subsystem_device":
        return f"{split_subsystem(rel_path)}/{split_device(rel_path)}"
    raise ValueError(f"Unknown group: {group}")


def main():
    parser = argparse.ArgumentParser(
        description="Count candidate cases per driver/device from drivers_candidates.txt."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="out/drivers_candidates.txt",
        help="Input candidates file (default: out/drivers_candidates.txt)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="out/drivers_candidates_counts.md",
        help="Output table file (default: out/drivers_candidates_counts.md)",
    )
    parser.add_argument(
        "--linux-root",
        default="/home/yqc5929/data_workspace/linux",
        help="Linux source root to strip from paths",
    )
    parser.add_argument(
        "--group",
        choices=[
            "file",
            "device",
            "dir",
            "subsystem",
            "subsystem_device",
        ],
        default="file",
        help="Grouping key for counting (default: file)",
    )
    parser.add_argument(
        "--format",
        choices=["md", "csv", "tsv"],
        default="md",
        help="Output table format (default: md)",
    )
    parser.add_argument(
        "--min-count",
        type=int,
        default=1,
        help="Minimum count to include (default: 1)",
    )
    args = parser.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        paths = list(parse_lines(f))

    rel_paths = [relpath(p, args.linux_root) for p in paths]
    counts = Counter(group_key(p, args.group) for p in rel_paths)

    # Build rows with context for file grouping.
    rows = []
    if args.group == "file":
        meta = {}
        for p in rel_paths:
            key = group_key(p, "file")
            if key not in meta:
                meta[key] = {
                    "device": split_device(p),
                    "subsystem": split_subsystem(p),
                }
        for key, cnt in counts.items():
            if cnt < args.min_count:
                continue
            rows.append(
                {
                    "key": key,
                    "count": cnt,
                    "device": meta[key]["device"],
                    "subsystem": meta[key]["subsystem"],
                }
            )
        rows.sort(key=lambda r: (-r["count"], r["key"]))
        headers = ["count", "subsystem", "device", "driver"]
        data_rows = [
            [str(r["count"]), r["subsystem"], r["device"], r["key"]]
            for r in rows
        ]
    elif args.group == "subsystem_device":
        for key, cnt in counts.items():
            if cnt < args.min_count:
                continue
            subsystem, device = key.split("/", 1) if "/" in key else (key, "")
            rows.append({"subsystem": subsystem, "device": device, "count": cnt})
        rows.sort(key=lambda r: (-r["count"], r["subsystem"], r["device"]))
        headers = ["count", "subsystem", "device"]
        data_rows = [[str(r["count"]), r["subsystem"], r["device"]] for r in rows]
    else:
        for key, cnt in counts.items():
            if cnt < args.min_count:
                continue
            rows.append({"key": key, "count": cnt})
        rows.sort(key=lambda r: (-r["count"], r["key"]))
        headers = ["count", args.group]
        data_rows = [[str(r["count"]), r["key"]] for r in rows]

    # Write table
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as out:
        if args.format == "md":
            out.write("| " + " | ".join(headers) + " |\n")
            out.write("| " + " | ".join(["---"] * len(headers)) + " |\n")
            for row in data_rows:
                out.write("| " + " | ".join(row) + " |\n")
        else:
            sep = "," if args.format == "csv" else "\t"
            out.write(sep.join(headers) + "\n")
            for row in data_rows:
                out.write(sep.join(row) + "\n")


if __name__ == "__main__":
    main()
