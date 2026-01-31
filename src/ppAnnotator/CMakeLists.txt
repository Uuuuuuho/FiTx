cmake_minimum_required(VERSION 3.1)

# Prefer clang/llvm-14 when available.
if(EXISTS "/usr/lib/llvm-14/lib/cmake/llvm" AND EXISTS "/usr/lib/llvm-14/lib/cmake/clang")
  set(LLVM_DIR "/usr/lib/llvm-14/lib/cmake/llvm")
  set(Clang_DIR "/usr/lib/llvm-14/lib/cmake/clang")
endif()

find_package(Clang REQUIRED CONFIG)

add_library(Annotator MODULE
  src/Annotator.cpp
)

target_include_directories(Annotator PRIVATE
  ${CLANG_INCLUDE_DIRS}
  ${LLVM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

 # Use default RTTI settings (Ubuntu clang-14 is built with RTTI).

target_link_libraries(Annotator PRIVATE
  clang-cpp
)
