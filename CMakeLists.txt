cmake_minimum_required(VERSION 3.20)

project(wasm_learn LANGUAGES C CXX)

include(CTest)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_program(CLANG_FORMAT NAMES clang-format-19 clang-format)
# An empty cache entry (for example from -DCLANG_TIDY=) hides a later install.
if(NOT CLANG_TIDY)
    unset(CLANG_TIDY CACHE)
endif()
find_program(CLANG_TIDY NAMES clang-tidy clang-tidy-19 clang-tidy-18)
find_program(AARCH64_GCC NAMES aarch64-linux-gnu-gcc)
find_program(AARCH64_GXX NAMES aarch64-linux-gnu-g++)
find_program(ARM32_GCC NAMES arm-linux-gnueabihf-gcc)

# Code dynamic checker. Reference: Chapter01/Target.md.
# Linker flags are required so AddressSanitizer is actually linked.
# Cross-compiled targets use explicit commands below and stay unsanitized.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

add_compile_options(-Wall -Wextra)

# Host programs. Binaries are written to the CMake build directory.
add_executable(hello Chapter00/hello.c)
add_executable(parse_wasm Chapter01/main.cpp)

if(BUILD_TESTING)
    add_test(
        NAME chapter02_arm64_gtest
        COMMAND bash ${CMAKE_SOURCE_DIR}/Chapter02/run_arm64_test.sh
    )
    set_tests_properties(chapter02_arm64_gtest PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

function(add_cross_program target compiler)
    cmake_parse_arguments(ARG "" "" "SOURCES;FLAGS;DEPENDS" ${ARGN})
    if(NOT compiler)
        message(FATAL_ERROR "Compiler for target '${target}' was not found")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "Target '${target}' has no sources")
    endif()

    set(output "${CMAKE_BINARY_DIR}/bin/${target}")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND "${compiler}" ${ARG_FLAGS} ${ARG_SOURCES} -o "${output}"
        DEPENDS ${ARG_SOURCES} ${ARG_DEPENDS}
        COMMENT "Building ${target}"
        VERBATIM
    )
    add_custom_target(${target} ALL DEPENDS "${output}")
endfunction()

set(AARCH64_C_FLAGS -Wall -Wextra -O0 -g)
set(AARCH64_ASM_FLAGS -Wall -Wextra -O0 -g -nostdlib -static)
set(ARM32_C_FLAGS -Wall -Wextra -O0 -g)
set(ARM32_ASM_FLAGS -Wall -Wextra -O0 -g -nostdlib -static)

file(GLOB CHAPTER02_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/com/*.hpp")
add_cross_program(assert_wasm ${AARCH64_GXX}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter02/chapter02_main.cpp
    FLAGS -std=c++17 ${AARCH64_C_FLAGS}
    DEPENDS ${CHAPTER02_HEADERS} ${CMAKE_SOURCE_DIR}/com/nlohmann/json.hpp
)

add_cross_program(mmap ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter01/mmap_main.c
    FLAGS ${AARCH64_C_FLAGS}
)
add_cross_program(hello64 ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/hello64.c
    FLAGS ${AARCH64_C_FLAGS}
)
add_cross_program(fibonacci64 ${AARCH64_GCC}
    SOURCES
        ${CMAKE_SOURCE_DIR}/Chapter00/fibonacci64.c
        ${CMAKE_SOURCE_DIR}/Chapter00/fibonacci64.s
    FLAGS ${AARCH64_C_FLAGS}
)
add_cross_program(hello32 ${ARM32_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/hello32.c
    FLAGS ${ARM32_C_FLAGS}
)

add_cross_program(asm64 ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/asm64.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(calling_convention_example ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/calling_convention_example.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(fibonacci64_call ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/fibonacci64_call.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(hello_asm ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/hello.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(register_play ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/register_play.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(simple_add ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/simple_add.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(sum64 ${AARCH64_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/sum64.s
    FLAGS ${AARCH64_ASM_FLAGS}
)
add_cross_program(asm32 ${ARM32_GCC}
    SOURCES ${CMAKE_SOURCE_DIR}/Chapter00/asm32.s
    FLAGS ${ARM32_ASM_FLAGS}
)

file(GLOB_RECURSE WASM_LEARN_CHECK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/*.c"
    "${CMAKE_SOURCE_DIR}/*.h"
    "${CMAKE_SOURCE_DIR}/*.cpp"
    "${CMAKE_SOURCE_DIR}/*.hpp"
)
list(FILTER WASM_LEARN_CHECK_SOURCES EXCLUDE REGEX "^${CMAKE_BINARY_DIR}/")
list(FILTER WASM_LEARN_CHECK_SOURCES EXCLUDE REGEX "/(build|\\.git)/")
list(FILTER WASM_LEARN_CHECK_SOURCES EXCLUDE REGEX "^${CMAKE_SOURCE_DIR}/com/nlohmann/")

set(WASM_LEARN_FORMAT_SOURCES ${WASM_LEARN_CHECK_SOURCES})

if(CLANG_FORMAT)
    add_custom_target(check-format
        COMMAND ${CLANG_FORMAT} --style=file --Werror --dry-run ${WASM_LEARN_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking formatting of all C/C++ files"
        VERBATIM
    )
else()
    add_custom_target(check-format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format was not found; install clang-format-19"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "clang-format is unavailable"
        VERBATIM
    )
endif()

if(CLANG_TIDY)
    set(WASM_LEARN_TIDY_SOURCES ${WASM_LEARN_CHECK_SOURCES})
    # This unused upstream excerpt requires src/config.hpp from its original project.
    list(FILTER WASM_LEARN_TIDY_SOURCES EXCLUDE REGEX "^${CMAKE_SOURCE_DIR}/com/util\\.hpp$")
    set(tidy_commands)
    foreach(source ${WASM_LEARN_TIDY_SOURCES})
        if(source MATCHES "\\.cpp$|\\.hpp$")
            set(tidy_standard -std=c++17)
        else()
            set(tidy_standard -std=c11)
        endif()
        list(APPEND tidy_commands
            COMMAND ${CLANG_TIDY}
                --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
                "--header-filter=${CMAKE_SOURCE_DIR}/(Chapter[^/]+|com)/[^/]+\\.(h|hpp)$"
                ${source}
                --
                ${tidy_standard}
        )
    endforeach()
    add_custom_target(check-tidy
        ${tidy_commands}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy on all C/C++ files"
        VERBATIM
    )
else()
    add_custom_target(check-tidy
        COMMAND ${CMAKE_COMMAND} -E echo "clang-tidy was not found; install clang-tidy to enable this check"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "clang-tidy is unavailable"
        VERBATIM
    )
endif()

# Runs the host programs built with the AddressSanitizer flags above.
add_custom_target(check-asan
    COMMAND ${CMAKE_COMMAND} -E env
        ASAN_OPTIONS=halt_on_error=1:detect_leaks=1:abort_on_error=1
        $<TARGET_FILE:hello>
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}/Chapter01"
        ${CMAKE_COMMAND} -E env
        ASAN_OPTIONS=halt_on_error=1:detect_leaks=1:abort_on_error=1
        $<TARGET_FILE:parse_wasm>
    DEPENDS hello parse_wasm
    COMMENT "Running Code dynamic checker (AddressSanitizer)"
    VERBATIM
)

add_custom_target(clang-tidy DEPENDS check-tidy)
add_custom_target(check DEPENDS check-format check-tidy check-asan)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/root-Makefile.in
    ${CMAKE_SOURCE_DIR}/Makefile
    @ONLY
)
