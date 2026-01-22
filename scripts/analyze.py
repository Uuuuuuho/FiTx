import datetime
import multiprocessing
import os
import subprocess
import time
from pathlib import Path

import click

from FiTx import utils
from FiTx.constants import FITX_ROOT, LINUX_ROOT, LOG_DIR, BUILD_DIR, DETECTOR_PATH

### CONSTANT VARIABLES ###
FITX_ROOT     = os.environ.get("FITX_ROOT", "/home/yqc5929/data_workspace/FiTx")
LINUX_ROOT    = os.environ.get("LINUX_ROOT", "/home/yqc5929/data_workspace/linux")
LOG_DIR       = os.path.abspath("./log")
BUILD_DIR     = os.path.join(FITX_ROOT, 'build')
DETECTOR_PATH = os.path.join(BUILD_DIR, 'detector', 'all_detector',
                             'libAllDetectorMod.so')

@click.group()
def commands():
    pass


@commands.command()
@click.option("--target", "-t", default=LINUX_ROOT)
@click.option("--file", "-f", default=None)
@click.option("--measure", "-m", is_flag=True)
def linux(target, file, measure):
    print(f"Start running analyzer")
    os.makedirs(LOG_DIR, exist_ok=True)
    tmplog = os.path.join(LOG_DIR, "tmplog")
    current = datetime.datetime.now().strftime('%Y_%m_%d_%H:%M')
    log = os.path.join(LOG_DIR, f"{current}.log")
    with open(tmplog, 'w+') as f:
        compiler_flags = ["-Xclang", "-load", "-Xclang", DETECTOR_PATH, "-flegacy-pass-manager"]
        make_flags = []

        if measure:
            compiler_flags += ["-mllvm", "-measure"]

        file_path = None
        if file:
            file_path = Path(file)
            abs_path = Path(target) / file_path
            if abs_path.is_dir():
                make_flags.append(f"M={file_path.as_posix()}")
                make_flags.append("modules")
            else:
                if abs_path.suffix == '.c':
                    make_flags.append(str(file_path.with_suffix('.o')))
                else:
                    make_flags.append(str(file_path))

        for make_target in make_flags:
            if make_target.startswith("M=") or make_target == "modules":
                continue
            target_file = os.path.join(target, make_target)
            if os.path.isfile(target_file):
                subprocess.run(['rm', target_file])

        command = utils.make_linux_build_command(
                target, multiprocessing.cpu_count(), compiler_flags, make_flags)

        # Start analysis
        start = time.time()
        result = subprocess.Popen(command, stderr=subprocess.PIPE, text=True)
        log_output = []
        stderr_lines = []
        with open(log, 'w+') as log_file:
            for line in result.stderr:
                stderr_lines.append(line)
                f.write(line)
                if "ERROR" in line or "LOG" in line:
                    log_file.write(line)
                    log_file.flush()
                    log_output.append(line)
        result.wait()
        if result.returncode != 0 and file_path and any("No rule to make target" in line for line in stderr_lines):
            retry_flags = [f"M={file_path.parent.as_posix()}", "modules"]
            retry_command = utils.make_linux_build_command(
                target, multiprocessing.cpu_count(), compiler_flags, retry_flags)
            retry = subprocess.Popen(retry_command, stderr=subprocess.PIPE, text=True)
            with open(log, 'a') as log_file:
                for line in retry.stderr:
                    f.write(line)
                    if "ERROR" in line or "LOG" in line:
                        log_file.write(line)
                        log_file.flush()
                        log_output.append(line)
            retry.wait()
        end = time.time()
        # Finish Analysis

    measure_output = ''
    measure_result = subprocess.run(['awk', "$0 ~/Elapsed/", tmplog], stdout=subprocess.PIPE)
    measure_output += measure_result.stdout.decode('utf-8')

    if not log_output:
        try:
            with open(tmplog, 'r') as f:
                fallback = f.read()
        except FileNotFoundError:
            fallback = ''
        with open(log, 'w+') as log_file:
            log_file.write(fallback)


    if measure:
        measure_log = os.path.join(LOG_DIR, f"{current}_time.log")
        with open(measure_log, 'w+') as f:
        #     subprocess.run(['awk', "$0 ~/Elapsed/", tmplog], stdout=f)
            # f.write(result.stdout.decode('utf-8'))
            f.write(measure_output)
        print(f"Logged time measure to {measure_log}")


    print(f"Done running command (Runtime - {end - start} sec): {command}")
    print(f"Logged to file {log}")


@commands.command()
@click.argument("target", type=click.Path(exists=True))
def test(target):
    print(f"Running test on {target}")
    target_files = utils.get_files(Path(target))
    additional_flags = ["-Xclang", "-load", "-Xclang", DETECTOR_PATH]

    tmplog = os.path.join(LOG_DIR, "tmplog")
    current = datetime.datetime.now().strftime('%Y_%m_%d_%H:%M')
    log = os.path.join(LOG_DIR, f"{current}.log")

    print(f"Found {len(target_files)} tests")
    with open(tmplog, 'w+') as f:
        for target in target_files:
            print(f"[Running] {target}\r", end="")
            compile_command = ["clang-14", target, "-o", "/dev/null",
                               "-flegacy-pass-manager"
                               ] + utils.compilation_flags(additional_flags)
            result = subprocess.run(compile_command, stderr=subprocess.PIPE)
            f.write(result.stderr.decode('utf-8'))

    logfiles = [tmplog]
    log_output = ''
    for logfile in logfiles:
        result = subprocess.run(['awk', "$0 ~/ERROR/ || $0 ~/LOG/", logfile],
                                stdout=subprocess.PIPE)
        log_output += utils.remove_redundant_log(result.stdout.decode('utf-8'))

    if not log_output:
        try:
            with open(tmplog, 'r') as f:
                log_output = f.read()
        except FileNotFoundError:
            log_output = ''

    with open(log, 'w+') as f:
        f.write(log_output)

    print(f"\nLogged to file {log}")


if __name__ == "__main__":
    commands()
