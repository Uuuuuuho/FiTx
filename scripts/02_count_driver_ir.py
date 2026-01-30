#!/usr/bin/env python3
import argparse
import os
from collections import Counter


def relpath(path, root):
    if root and path.startswith(root.rstrip("/") + "/"):
        return path[len(root.rstrip("/")) + 1 :]
    return path


def split_subsystem(rel_path):
    parts = rel_path.split("/")
    if len(parts) >= 2 and parts[0] == "drivers":
        return parts[1]
    return parts[0] if parts else ""


def split_device(rel_path):
    parent = os.path.dirname(rel_path)
    return os.path.basename(parent)


def ll_to_c_path(rel_path):
    if rel_path.endswith("_symbolic.ll"):
        return rel_path[: -len("_symbolic.ll")] + ".c"
    return rel_path


def group_key(rel_path, group):
    if group == "total":
        return "total"
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
    if group == "list":
        return rel_path
    raise ValueError(f"Unknown group: {group}")


def iter_ll_files(root_dir):
    for dirpath, _, filenames in os.walk(root_dir):
        for name in filenames:
            if name.endswith(".ll"):
                yield os.path.join(dirpath, name)


def write_table(path, headers, rows, fmt):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as out:
        if fmt == "md":
            out.write("| " + " | ".join(headers) + " |\n")
            out.write("| " + " | ".join(["---"] * len(headers)) + " |\n")
            for row in rows:
                out.write("| " + " | ".join(row) + " |\n")
        else:
            sep = "," if fmt == "csv" else "\t"
            out.write(sep.join(headers) + "\n")
            for row in rows:
                out.write(sep.join(row) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Count LLVM IR (.ll) files under pp/driver_out/drivers."
    )
    parser.add_argument(
        "--root",
        default="pp/driver_out/drivers",
        help="Root directory to scan (default: pp/driver_out/drivers)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="out/driver_ir_counts.md",
        help="Output table file (default: out/driver_ir_counts.md)",
    )
    parser.add_argument(
        "--strip-root",
        default="pp/driver_out",
        help="Root prefix to strip from paths in table (default: pp/driver_out)",
    )
    parser.add_argument(
        "--group",
        choices=[
            "list",
            "total",
            "file",
            "device",
            "dir",
            "subsystem",
            "subsystem_device",
        ],
        default="list",
        help="Grouping key for output (default: list)",
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

    ll_files = list(iter_ll_files(args.root))
    rel_paths = [relpath(p, args.strip_root) for p in ll_files]
    counts = Counter(group_key(p, args.group) for p in rel_paths)

    rows = []
    if args.group == "list":
        # One row per .ll file
        rows = []
        for p in rel_paths:
            subsystem = split_subsystem(p)
            device = split_device(p)
            driver = ll_to_c_path(p)
            rows.append({"subsystem": subsystem, "device": device, "driver": driver})
        rows.sort(key=lambda r: (r["subsystem"], r["device"], r["driver"]))
        headers = ["count", "subsystem", "device", "driver"]
        data_rows = [
            ["1", r["subsystem"], r["device"], r["driver"]] for r in rows
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
    elif args.group == "total":
        total = counts.get("total", 0)
        headers = ["count"]
        data_rows = [[str(total)]]
    else:
        for key, cnt in counts.items():
            if cnt < args.min_count:
                continue
            rows.append({"key": key, "count": cnt})
        rows.sort(key=lambda r: (-r["count"], r["key"]))
        headers = ["count", args.group]
        data_rows = [[str(r["count"]), r["key"]] for r in rows]

    write_table(args.output, headers, data_rows, args.format)


if __name__ == "__main__":
    main()
