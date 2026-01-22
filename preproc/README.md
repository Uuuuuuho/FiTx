# FiTx Preproc Pipeline (C++/LLVM)

This directory provides a C++/LLVM-based prototype pipeline for symbolic branch lowering of variational C code with `#ifdef` directives. It builds C++ tools, runs PC (Presence Condition) propagation and symbolic lowering, emits LLVM IR, and compares baseline vs symbolic IR outputs.

## Directory Structure

```
preproc/
  build.sh
  run_test.sh
  build/
    emit_ir
    ir_summary
    pc_propagation_clang
    symbolic_lowering_clang
  src/
    emit_ir.cpp
    ir_summary.cpp
    pc_propagation_clang.cpp
    symbolic_lowering_clang.cpp
  test/
    example1_alloc_free.c
    example2_branch.c
    example3_struct.c
    example4_function.c
    example5_macro.c
```

## Tools

- `pc_propagation_clang`: Uses Clang frontend callbacks to compute presence conditions and annotate source with `// PC:` markers.
- `symbolic_lowering_clang`: Lowers `#ifdef` blocks into explicit symbolic branches while keeping both sides.
- `emit_ir`: Emits LLVM IR via Clang frontend and writes `.ll` files.
- `ir_summary`: Reads LLVM IR via LLVM API and summarizes functions/structs/calls.

## Build and Run

### Build a single example

```bash
cd /home/yqc5929/data_workspace/FiTx/preproc
./build.sh test/example1_alloc_free.c out/example1_alloc_free CONFIG_USB
```

Outputs in `out/example1_alloc_free/`:
- `*_baseline.ll`: IR from normal preprocessing
- `*_pc.c`: PC-annotated source
- `*_symbolic.c`: lowered symbolic source
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

## Notes

- `build.sh` auto-builds C++ tools when missing or out-of-date.
- `run_test.sh` defaults to binaries under `build/`.
- Warnings about missing compilation databases can appear; they do not affect pipeline execution.
