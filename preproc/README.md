# FiTx Preproc Pipeline (Clang Plugin Method A)

This directory provides a Clang-plugin-based pipeline for symbolic branch lowering of variational C code with `#ifdef` directives. The plugin runs inside the Clang frontend, emits intermediate artifacts (PC-annotated and lowered source), and the pipeline then emits LLVM IR for baseline vs symbolic builds.

## Directory Structure

```
preproc/
  CMakeLists.txt
  build.sh
  run_test.sh
  header/
    DirEnum.h
    PPCapture.h
    PcLowerPluginAction.h
  src/
    PPCapture.cpp
    PcLwoerPluginAction.cpp
    ir_summary.cpp
  build/
    pclower_plugin.so
    ir_summary
  test/
    example1_alloc_free.c
    example2_branch.c
    example3_struct.c
    example4_function.c
    example5_macro.c
```

## Tools

- `pclower_plugin.so`: Clang frontend plugin. It captures `#ifdef` regions and emits:
  - PC-annotated source (`*_pc.c`)
  - Lowered symbolic source (`*_symbolic.c`)
- `ir_summary`: Reads LLVM IR via LLVM API and summarizes functions/structs/calls.

## Build and Run

### Build tools with CMake

```bash
cd /home/yqc5929/data_workspace/FiTx/preproc
cmake -S . -B build
cmake --build build
```

### Build a single example

```bash
cd /home/yqc5929/data_workspace/FiTx/preproc
./build.sh test/example1_alloc_free.c out/example1_alloc_free CONFIG_USB
```

Outputs in `out/example1_alloc_free/`:
- `*_baseline.ll`: IR from normal preprocessing
- `*_pc.c`: PC-annotated source (from plugin)
- `*_symbolic.c`: lowered symbolic source (from plugin)
- `*_symbolic.ll`: symbolic IR

### Run the full test suite

```bash
cd ~/../preproc
./run_test.sh
```

The test script:
- builds all examples
- emits baseline and symbolic IR
- prints IR summaries and diffs
- runs generated binaries

## Example Coverage

1. Alloc/free variation (`example1_alloc_free.c`)
2. Branch/control variation (`example2_branch.c`)
3. Struct field variation (`example3_struct.c`)
4. Function availability (`example4_function.c`)
5. Macro wrapper variability (`example5_macro.c`)

## Pipeline Steps

1) **Clang plugin pass** (inside Clang frontend)
- `clang -Xclang -load -Xclang build/pclower_plugin.so -Xclang -plugin -Xclang pclower ...`
- Emits `*_pc.c` and `*_symbolic.c`

2) **IR emission**
- Baseline: `clang -S -emit-llvm` on original input
- Symbolic: `clang -S -emit-llvm` on lowered `*_symbolic.c`

3) **IR summary**
- `build/ir_summary` prints functions/structs/calls

## Notes

- Tools are built by CMake into `build/`.
- `build.sh` will invoke CMake if the binaries are missing.
- `run_test.sh` defaults to binaries under `build/`.
- Macro redefinition warnings may appear for the macro-wrapper example; they do not affect correctness.
