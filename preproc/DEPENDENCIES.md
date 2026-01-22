# Dependencies (Minimal)

## Required

- **LLVM dev libraries**: provides `llvm-config`, LLVM headers, and `libLLVM` components
- **Clang dev libraries**: provides Clang headers and `libclang-cpp.so*`
- **C++ compiler**: `clang++` (default) or `g++`
- **CMake/Build tools**: not required (build uses direct `clang++` commands)

## Runtime

- **clang**: required for compiling test binaries produced from C sources

## Notes

- `tools/build_cpp_tools.sh` prefers `llvm-config --libs clangTooling`; if not available, it links directly to `libclang-cpp.so*` from `llvm-config --libdir`.
- If `libclang-cpp.so*` is missing, install your distro's Clang development package (e.g., `libclang-dev`, `clang-tools`).
