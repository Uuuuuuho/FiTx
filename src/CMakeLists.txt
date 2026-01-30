cmake_minimum_required(VERSION 3.1)

# Prefer clang-14 toolchain unless user overrides via -DCMAKE_*_COMPILER.
if(NOT DEFINED CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER clang-14 CACHE STRING "C compiler" FORCE)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER clang++-14 CACHE STRING "C++ compiler" FORCE)
endif()

project(MyProj)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer LLVM/Clang 14 if available; allow override via -DLLVM_DIR/-DClang_DIR.
if(NOT DEFINED LLVM_DIR)
  set(LLVM_DIR "/usr/lib/llvm-14/lib/cmake/llvm" CACHE PATH "LLVM 14 CMake dir" FORCE)
endif()
if(NOT DEFINED Clang_DIR)
  set(Clang_DIR "/usr/lib/llvm-14/lib/cmake/clang" CACHE PATH "Clang 14 CMake dir" FORCE)
endif()

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

set(FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/framework")
set(DETECTOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/detector")
set(TYPE_DETERMINATOR_DIR "${FRAMEWORK_DIR}/type_determinator")

add_subdirectory(framework)
add_subdirectory(detector)
add_subdirectory(ppAnnotator)
add_subdirectory(ppLower)