#!/usr/bin/env python3
import argparse
import os
from collections import defaultdict
import re


def iter_c_files(root):
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if name.endswith(".c"):
                yield os.path.join(dirpath, name)


def relpath(path, root):
    root = root.rstrip("/")
    if path.startswith(root + "/"):
        return path[len(root) + 1 :]
    return path


def split_device(rel_path):
    parent = os.path.dirname(rel_path)
    return os.path.basename(parent)


def device_dir(rel_path):
    return os.path.dirname(rel_path)


def parse_candidates_file(path, linux_root):
    # Expect lines like: /path/to/linux/drivers/.../foo.c:123 (cfg:...)
    candidate_re = re.compile(r"^(?P<path>.*?\.c):\d+")
    device_dirs = set()
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = candidate_re.match(line.strip())
            if not m:
                continue
            c_path = m.group("path")
            rel_c = relpath(c_path, linux_root)
            if rel_c.endswith(".c"):
                device_dirs.add(device_dir(rel_c))
    return device_dirs


def symbolic_ll_path(rel_c_path, driver_out_root):
    # rel_c_path like drivers/.../foo.c
    base = rel_c_path[:-2]  # strip .c
    return os.path.join(driver_out_root, base + "_symbolic.ll")


def write_md_table(path, headers, rows):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as out:
        out.write("| " + " | ".join(headers) + " |\n")
        out.write("| " + " | ".join(["---"] * len(headers)) + " |\n")
        for row in rows:
            out.write("| " + " | ".join(row) + " |\n")


def main():
    parser = argparse.ArgumentParser(
        description="Compute LLVM IR compile success rate vs Linux drivers tree."
    )
    parser.add_argument(
        "--linux-root",
        default="/home/yqc5929/data_workspace/linux",
        help="Linux source root (default: /home/yqc5929/data_workspace/linux)",
    )
    parser.add_argument(
        "--driver-out",
        default="pp/driver_out",
        help="Driver out root containing drivers/ (default: pp/driver_out)",
    )
    parser.add_argument(
        "--target-subdir",
        default="drivers",
        help="Subdirectory under linux root to compare (default: drivers)",
    )
    parser.add_argument(
        "--candidates-file",
        default="",
        help="Optional candidates file (e.g. out/drivers_candidates.txt) to filter drivers",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="out/driver_ir_success_rate.md",
        help="Output markdown file (default: out/driver_ir_success_rate.md)",
    )
    args = parser.parse_args()

    linux_root = args.linux_root.rstrip("/")
    target_root = os.path.join(linux_root, args.target_subdir)
    driver_out_root = os.path.join(args.driver_out.rstrip("/"), args.target_subdir)

    if not os.path.isdir(target_root):
        raise SystemExit(f"Target directory not found: {target_root}")
    if not os.path.isdir(driver_out_root):
        raise SystemExit(f"Driver out directory not found: {driver_out_root}")

    total_dirs = 0
    complete_dirs = 0
    by_device = defaultdict(lambda: {"total": 0, "success": 0})

    candidate_device_dirs = None
    if args.candidates_file:
        if not os.path.isfile(args.candidates_file):
            raise SystemExit(f"Candidates file not found: {args.candidates_file}")
        candidate_device_dirs = parse_candidates_file(args.candidates_file, linux_root)

    rel_c_paths = [relpath(p, linux_root) for p in iter_c_files(target_root)]

    for rel_c in rel_c_paths:
        if not rel_c.startswith(args.target_subdir.rstrip("/") + "/"):
            continue
        dev_dir = device_dir(rel_c)
        if candidate_device_dirs is not None and dev_dir not in candidate_device_dirs:
            continue
        ll_path = symbolic_ll_path(rel_c, args.driver_out.rstrip("/"))
        ok = os.path.isfile(ll_path) and os.path.getsize(ll_path) > 0
        if ok:
            by_device[dev_dir]["success"] += 1
        by_device[dev_dir]["total"] += 1

    rows = []
    if candidate_device_dirs is not None:
        total_dirs = len(candidate_device_dirs)
        for dev_dir in candidate_device_dirs:
            counts = by_device.get(dev_dir, {"total": 0, "success": 0})
            if counts["total"] > 0 and counts["success"] == counts["total"]:
                complete_dirs += 1
    else:
        for dev_dir, counts in by_device.items():
            total_dirs += 1
            if counts["total"] > 0 and counts["success"] == counts["total"]:
                complete_dirs += 1
    overall_rate = (complete_dirs / total_dirs * 100.0) if total_dirs else 0.0
    total_files = sum(c["total"] for c in by_device.values())
    success_files = sum(c["success"] for c in by_device.values())
    file_rate = (success_files / total_files * 100.0) if total_files else 0.0
    rows.append(
        [
            str(total_dirs),
            str(complete_dirs),
            f"{overall_rate:.2f}%",
            str(total_files),
            str(success_files),
            f"{file_rate:.2f}%",
        ]
    )

    table_rows = []
    for dev_dir, counts in by_device.items():
        t = counts["total"]
        s = counts["success"]
        rate = (s / t * 100.0) if t else 0.0
        table_rows.append([dev_dir, str(t), str(s), f"{rate:.2f}%"])
    table_rows.sort(key=lambda r: (-float(r[3].rstrip("%")), r[0]))

    # Write combined output
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as out:
        out.write("## Overall\n\n")
        out.write("| total_device_dirs | complete_dirs | completion_rate | total_files | success_files | file_success_rate |\n")
        out.write("| --- | --- | --- | --- | --- | --- |\n")
        out.write("| " + " | ".join(rows[0]) + " |\n\n")
        out.write("## By Device\n\n")
        out.write("| device_dir | total_files | success_files | success_rate |\n")
        out.write("| --- | --- | --- | --- |\n")
        for row in table_rows:
            out.write("| " + " | ".join(row) + " |\n")


if __name__ == "__main__":
    main()
