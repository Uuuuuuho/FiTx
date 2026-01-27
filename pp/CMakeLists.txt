cmake_minimum_required(VERSION 3.16)
project(fitx_preproc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_program(LLVM_CONFIG llvm-config REQUIRED)

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
        execute_process(
            COMMAND sh -c "ldconfig -p | grep -m1 'libclang-cpp.so' | awk '{print \$NF}'"
            OUTPUT_VARIABLE CLANG_CPP_LIB
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE LD_STATUS)
        if(NOT LD_STATUS EQUAL 0)
            set(CLANG_CPP_LIB "")
        endif()
    endif()
    if(NOT CLANG_CPP_LIB)
        message(FATAL_ERROR "Clang libraries not found. Install clang dev packages or set CLANG_CPP_LIB.")
    endif()
    set(CLANG_LINK_LIBS ${CLANG_CPP_LIB})
endif()

set(COMMON_COMPILE_OPTS ${LLVM_CXXFLAGS_LIST})
set(COMMON_LINK_OPTS ${LLVM_LDFLAGS_LIST} ${LLVM_SYSTEM_LIBS_LIST})

add_library(pclower_plugin SHARED
    src/PcLwoerPluginAction.cpp
    src/LoweredRenderer.cpp
    src/PPCapture.cpp
)

target_include_directories(pclower_plugin PRIVATE
    ${LLVM_INCLUDE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/header
)

target_compile_options(pclower_plugin PRIVATE ${COMMON_COMPILE_OPTS})

target_link_options(pclower_plugin PRIVATE ${COMMON_LINK_OPTS})

target_link_libraries(pclower_plugin PRIVATE
    ${CLANG_LINK_LIBS}
    ${LLVM_CORE_LIBS_LIST}
)

set_target_properties(pclower_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "pclower_plugin"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(ir_summary src/ir_summary.cpp)

target_include_directories(ir_summary PRIVATE
    ${LLVM_INCLUDE_DIR}
)

target_compile_options(ir_summary PRIVATE ${COMMON_COMPILE_OPTS})

target_link_options(ir_summary PRIVATE ${COMMON_LINK_OPTS})

target_link_libraries(ir_summary PRIVATE
    ${LLVM_IR_LIBS_LIST}
)

set_target_properties(ir_summary PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
