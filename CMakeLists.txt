cmake_minimum_required(VERSION 3.24)

project(amigaport VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AMIGAPORT_BUILD_TESTS "Build focused runtime tests" ON)

set(AMIGAPORT_PUAE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libretro-uae" CACHE PATH
    "Pinned maintained libretro-uae checkout")
set(AMIGAPORT_PUAE_REVISION "bae4dbe42dfef782d679eccf5e62c9622ebb8dd9")

foreach(required_path
        "${AMIGAPORT_PUAE_DIR}/COPYING"
        "${AMIGAPORT_PUAE_DIR}/sources/src/cpuemu_0.c"
        "${AMIGAPORT_PUAE_DIR}/sources/src/include/newcpu.h")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "amigaport requires the pinned maintained libretro-uae submodule; missing ${required_path}. "
            "Run: git submodule update --init --recursive")
    endif()
endforeach()

execute_process(
    COMMAND git -C "${AMIGAPORT_PUAE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE AMIGAPORT_PUAE_HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT AMIGAPORT_PUAE_HEAD STREQUAL AMIGAPORT_PUAE_REVISION)
    message(FATAL_ERROR
        "libretro-uae revision mismatch: expected ${AMIGAPORT_PUAE_REVISION}, got "
        "${AMIGAPORT_PUAE_HEAD}")
endif()

add_library(amigaport_puae_cpu OBJECT
    "${AMIGAPORT_PUAE_DIR}/sources/src/cpuemu_0.c")
target_compile_definitions(amigaport_puae_cpu PRIVATE
    __LIBRETRO__
    CPUEMU_68000_ONLY)
target_compile_options(amigaport_puae_cpu PRIVATE
    -ffunction-sections
    -fdata-sections
    $<$<C_COMPILER_ID:Clang>:-Wno-comment>)
target_include_directories(amigaport_puae_cpu PRIVATE
    "${AMIGAPORT_PUAE_DIR}/sources/src"
    "${AMIGAPORT_PUAE_DIR}/sources/src/include"
    "${AMIGAPORT_PUAE_DIR}/retrodep"
    "${AMIGAPORT_PUAE_DIR}"
    "${AMIGAPORT_PUAE_DIR}/deps/7zip"
    "${AMIGAPORT_PUAE_DIR}/deps/libmpeg2/include"
    "${AMIGAPORT_PUAE_DIR}/libretro"
    "${AMIGAPORT_PUAE_DIR}/libretro-common/include"
    "${AMIGAPORT_PUAE_DIR}/libretro-common/include/compat/zlib")

add_library(amigaport STATIC
    src/cpu_state.cpp
    src/executor.cpp
    src/override_registry.cpp
    src/puae_bridge.c
    src/puae_core.cpp
    $<TARGET_OBJECTS:amigaport_puae_cpu>)
add_library(amigaport::amigaport ALIAS amigaport)
target_include_directories(amigaport
    PUBLIC include
    PRIVATE
        src
        "${AMIGAPORT_PUAE_DIR}/sources/src"
        "${AMIGAPORT_PUAE_DIR}/sources/src/include"
        "${AMIGAPORT_PUAE_DIR}/retrodep"
        "${AMIGAPORT_PUAE_DIR}"
        "${AMIGAPORT_PUAE_DIR}/deps/7zip"
        "${AMIGAPORT_PUAE_DIR}/deps/libmpeg2/include"
        "${AMIGAPORT_PUAE_DIR}/libretro"
        "${AMIGAPORT_PUAE_DIR}/libretro-common/include"
        "${AMIGAPORT_PUAE_DIR}/libretro-common/include/compat/zlib")
target_compile_options(amigaport PRIVATE
    -ffunction-sections
    -fdata-sections
    $<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:Clang>>:-Wno-comment>)
target_link_options(amigaport INTERFACE
    $<$<OR:$<PLATFORM_ID:Linux>,$<PLATFORM_ID:Android>>:-Wl,--gc-sections>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>)

if(AMIGAPORT_BUILD_TESTS)
    enable_testing()
    add_executable(amigaport_tests
        tests/executor_test.cpp
        tests/test_support.cpp)
    target_include_directories(amigaport_tests PRIVATE tests)
    target_link_libraries(amigaport_tests PRIVATE amigaport)
    add_test(NAME amigaport_tests COMMAND amigaport_tests)
endif()
