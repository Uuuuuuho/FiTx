# FiTx Preproc Prototype: Symbolic Branch Lowering

This prototype builds a pipeline that:
1. Parses variational C source with `#ifdef/#ifndef/#else/#endif`.
2. Computes presence conditions (PC) and annotates source.
3. Lowers `#ifdef` blocks into explicit symbolic branches, keeping both sides.
4. Emits LLVM IR for a **baseline** build (preprocessor deletes code) and a **symbolic** build (both branches kept).
5. Compares IR to highlight differences in call sites, struct layouts, and availability.

## Directory Layout

```
preproc/
  test/
    example1_alloc_free.c
    example2_branch.c
    example3_struct.c
    example4_function.c
    example5_macro.c
  src/
    pc_propagation_clang.cpp
    symbolic_lowering_clang.cpp
    emit_ir.cpp
    ir_summary.cpp    
  build.sh
  run_tests.sh
  README.md
```

## Pipeline Overview

- **PC propagation** (`src/pc_propagation_clang`): Adds `// PC: ...` markers to show active conditions using Clang frontend callbacks.
- **Symbolic lowering** (`src/symbolic_lowering_clang`): Rewrites `#ifdef` blocks to `if (__cfg("MACRO"))` branches and inserts `PC_ANNOTATE("PC: ...")` markers using Clang frontend callbacks.
- **LLVM IR emission** (`src/emit_ir`): Uses `EmitLLVMOnlyAction` from Clang to emit LLVM IR.
- **IR summary** (`src/ir_summary`): Parses IR via LLVM API (`IRReader`).

## Example Categories Covered

1. Call-site + alloc/free variation (`example1_alloc_free.c`)
2. Branch/control variation (`example2_branch.c`)
3. Struct field variation (`example3_struct.c`)
4. Function availability (`example4_function.c`)
5. Macro wrapper variability (`example5_macro.c`)

## How to Run

### Build a single example

```bash
cd /home/yqc5929/data_workspace/FiTx/preproc
./build.sh
```

Artifacts are created in `out/example1_alloc_free/`:
- `*_baseline.ll`: baseline IR (preprocessor deletes non-selected branches)
- `*_pc.c`: PC-annotated source
- `*_symbolic.c`: lowered symbolic source
- `*_symbolic.ll`: symbolic IR (both branches kept)

### Run all examples

```bash
cd /home/yqc5929/data_workspace/FiTx/preproc
./run_test.sh
```

## Expected Outputs & Differences

- **PC propagation**: `*_pc.c` includes `// PC:` markers that represent presence conditions.
- **Symbolic IR**: contains both sides of `#ifdef` regions, plus `llvm.var.annotation` calls from `PC_ANNOTATE`.
- **Struct layout**: `example3_struct` shows struct field differences between baseline and symbolic IR.
- **Function availability**: `example4_function` shows `foo` missing in baseline when `CONFIG_X` is unset and present symbolically.
- **Macro wrappers**: `example5_macro` shows call to `refcount_inc` appearing symbolically even if baseline removes it.

## Notes

- Requires `clang` and **Clang/LLVM development libraries** on Linux.
- `./tools/build_cpp_tools.sh` first tries `llvm-config --libs clangTooling`; if unavailable, it falls back to linking `libclang-cpp` (by full path if no symlink exists).
- If both fail, install your distro's Clang dev packages (e.g., `libclang-dev`, `clang-tools`).
- The helper function `__cfg()` ensures symbolic branches are retained in LLVM IR.
