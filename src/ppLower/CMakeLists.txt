cmake_minimum_required(VERSION 3.16)
project(fitx_preproc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_program(LLVM_CONFIG llvm-config-14 llvm-config REQUIRED)

execute_process(COMMAND ${LLVM_CONFIG} --includedir
                OUTPUT_VARIABLE LLVM_INCLUDE_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libdir
                OUTPUT_VARIABLE LLVM_LIB_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${LLVM_CONFIG} --cxxflags
                OUTPUT_VARIABLE LLVM_CXXFLAGS
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --ldflags
                OUTPUT_VARIABLE LLVM_LDFLAGS
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --system-libs
                OUTPUT_VARIABLE LLVM_SYSTEM_LIBS
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs core
                OUTPUT_VARIABLE LLVM_CORE_LIBS
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs core irreader
                OUTPUT_VARIABLE LLVM_IR_LIBS
                OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(LLVM_CXXFLAGS_LIST NATIVE_COMMAND "${LLVM_CXXFLAGS}")
separate_arguments(LLVM_LDFLAGS_LIST NATIVE_COMMAND "${LLVM_LDFLAGS}")
separate_arguments(LLVM_SYSTEM_LIBS_LIST NATIVE_COMMAND "${LLVM_SYSTEM_LIBS}")
separate_arguments(LLVM_CORE_LIBS_LIST NATIVE_COMMAND "${LLVM_CORE_LIBS}")
separate_arguments(LLVM_IR_LIBS_LIST NATIVE_COMMAND "${LLVM_IR_LIBS}")

execute_process(
    COMMAND ${LLVM_CONFIG} --libs clangTooling clangFrontend clangAST clangBasic
            clangRewrite clangLex clangParse clangCodeGen
    RESULT_VARIABLE CLANG_LIBS_STATUS
    OUTPUT_VARIABLE CLANG_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CLANG_CPP_LIB "" CACHE FILEPATH "Path to libclang-cpp")

# If the system has libclang-cpp.so.14 installed, prefer it explicitly.
if(NOT CLANG_CPP_LIB AND EXISTS "/lib/x86_64-linux-gnu/libclang-cpp.so.14")
    set(CLANG_CPP_LIB "/lib/x86_64-linux-gnu/libclang-cpp.so.14")
elseif(NOT CLANG_CPP_LIB AND EXISTS "/usr/lib/llvm-14/lib/libclang-cpp.so.14")
    set(CLANG_CPP_LIB "/usr/lib/llvm-14/lib/libclang-cpp.so.14")
endif()

if(CLANG_LIBS_STATUS EQUAL 0)
    separate_arguments(CLANG_LIBS_LIST NATIVE_COMMAND "${CLANG_LIBS}")
    set(CLANG_LINK_LIBS ${CLANG_LIBS_LIST})
else()
    if(NOT CLANG_CPP_LIB)
        find_library(
            CLANG_CPP_LIB
            NAMES clang-cpp libclang-cpp
            HINTS ${LLVM_LIB_DIR}
            PATHS /lib /usr/lib /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu
            PATH_SUFFIXES llvm lib)
    endif()
    if(NOT CLANG_CPP_LIB)
        # Prefer libclang-cpp.so.14 when available (matching clang-14)
        if(EXISTS "/usr/lib/llvm-14/lib/libclang-cpp.so.14")
            set(CLANG_CPP_LIB "/usr/lib/llvm-14/lib/libclang-cpp.so.14")
        elseif(EXISTS "/lib/x86_64-linux-gnu/libclang-cpp.so.14")
            set(CLANG_CPP_LIB "/lib/x86_64-linux-gnu/libclang-cpp.so.14")
        elseif(EXISTS "/usr/lib/libclang-cpp.so.14")
            set(CLANG_CPP_LIB "/usr/lib/libclang-cpp.so.14")
        else()
            execute_process(
                COMMAND sh -c "ldconfig -p | grep -m1 'libclang-cpp.so' | awk '{print \$NF}'"
                OUTPUT_VARIABLE CLANG_CPP_LIB
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE LD_STATUS)
        endif()
        if(NOT LD_STATUS EQUAL 0)
            set(CLANG_CPP_LIB "")
        endif()
    endif()
    if(NOT CLANG_CPP_LIB)
        message(FATAL_ERROR "Clang libraries not found. Install clang dev packages or set CLANG_CPP_LIB.")
    endif()
    set(CLANG_LINK_LIBS ${CLANG_CPP_LIB})
endif()

## Filter out any -std= flags from llvm-config cxxflags so we can control the standard
set(FILTERED_LLVM_CXXFLAGS)
foreach(_flag ${LLVM_CXXFLAGS_LIST})
    if(NOT _flag MATCHES "^-std=")
        list(APPEND FILTERED_LLVM_CXXFLAGS ${_flag})
    endif()
endforeach()
set(COMMON_COMPILE_OPTS ${FILTERED_LLVM_CXXFLAGS})
set(COMMON_LINK_OPTS ${LLVM_LDFLAGS_LIST} ${LLVM_SYSTEM_LIBS_LIST})

add_library(PcLower SHARED
    src/PcLowerPluginAction.cpp
    src/LoweredRenderer.cpp
    src/PPCapture.cpp
)

# Enforce C++17 for the plugin target (guard against llvm-config cxxflags overriding)
target_compile_features(PcLower PUBLIC cxx_std_17)
target_compile_options(PcLower PRIVATE -std=c++17)

target_include_directories(PcLower PRIVATE
    ${LLVM_INCLUDE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/header
)

target_compile_options(PcLower PRIVATE ${COMMON_COMPILE_OPTS})
# Apply -std=c++17 last so it overrides any earlier -std flags
target_compile_options(PcLower PRIVATE -std=c++17)

target_link_options(PcLower PRIVATE ${COMMON_LINK_OPTS})

target_link_libraries(PcLower PRIVATE
    ${CLANG_LINK_LIBS}
    ${LLVM_CORE_LIBS_LIST}
)
