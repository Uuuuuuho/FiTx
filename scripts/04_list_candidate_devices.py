#!/usr/bin/env python3
import argparse
import os
import re


def relpath(path, root):
    root = root.rstrip("/")
    if path.startswith(root + "/"):
        return path[len(root) + 1 :]
    return path


def split_device(rel_path):
    return os.path.basename(os.path.dirname(rel_path))


def device_dir(rel_path):
    return os.path.dirname(rel_path)


def parse_candidates_devices(path, linux_root):
    # Expect lines like: /path/to/linux/drivers/.../foo.c:123 (cfg:...)
    candidate_re = re.compile(r"^(?P<path>.*?\.c):\d+")
    devices = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = candidate_re.match(line.strip())
            if not m:
                continue
            c_path = m.group("path")
            rel_c = relpath(c_path, linux_root)
            if rel_c.endswith(".c"):
                dev = split_device(rel_c)
                rel_dir = device_dir(rel_c)
                devices.setdefault(dev, set()).add(rel_dir)
    return devices


def count_files_in_dir(dir_path):
    try:
        return sum(
            1
            for name in os.listdir(dir_path)
            if os.path.isfile(os.path.join(dir_path, name))
        )
    except FileNotFoundError:
        return 0


def write_output(path, device_rows, fmt):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as out:
        if fmt == "md":
            out.write("| device | file_count |\n")
            out.write("| --- | --- |\n")
            for dev, count in device_rows:
                out.write(f"| {dev} | {count} |\n")
        elif fmt == "csv":
            out.write("device,file_count\n")
            for dev, count in device_rows:
                out.write(f"{dev},{count}\n")
        elif fmt == "tsv":
            out.write("device\tfile_count\n")
            for dev, count in device_rows:
                out.write(f"{dev}\t{count}\n")
        else:
            for dev, count in device_rows:
                out.write(f"{dev}\t{count}\n")


def main():
    parser = argparse.ArgumentParser(
        description="List device names from a candidates file."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="out/drivers_candidates.txt",
        help="Candidates file (default: out/drivers_candidates.txt)",
    )
    parser.add_argument(
        "--linux-root",
        default="/home/yqc5929/data_workspace/linux",
        help="Linux source root to strip from paths",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="out/candidate_devices.md",
        help="Output file (default: out/candidate_devices.md)",
    )
    parser.add_argument(
        "--format",
        choices=["txt", "md", "csv", "tsv"],
        default="md",
        help="Output format (default: md)",
    )
    args = parser.parse_args()

    devices = parse_candidates_devices(args.input, args.linux_root)
    device_rows = []
    for dev, rel_dirs in devices.items():
        count = 0
        for rel_dir in rel_dirs:
            abs_dir = os.path.join(args.linux_root, rel_dir)
            count += count_files_in_dir(abs_dir)
        device_rows.append((dev, count))
    device_rows.sort(key=lambda r: (-r[1], r[0]))
    write_output(args.output, device_rows, args.format)


if __name__ == "__main__":
    main()
