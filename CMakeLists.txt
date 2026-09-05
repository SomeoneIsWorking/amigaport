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
set(AMIGAPORT_PUAE_REVISION "1f2e0a209fa3a147dfe7b2ab4fdb05ab80a7a495")

foreach(required_path
        "${AMIGAPORT_PUAE_DIR}/COPYING"
        "${AMIGAPORT_PUAE_DIR}/embed/CMakeLists.txt"
        "${AMIGAPORT_PUAE_DIR}/sources/src/include/uae/m68k_embed.h")
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

if(AMIGAPORT_BUILD_TESTS)
    set(PUAE_M68K_EMBED_BUILD_TESTS ON)
endif()
add_subdirectory("${AMIGAPORT_PUAE_DIR}/embed" third_party/libretro-uae)

add_library(amigaport STATIC
    src/cpu_state.cpp
    src/executor.cpp
    src/override_registry.cpp
    src/puae_core.cpp)
add_library(amigaport::amigaport ALIAS amigaport)
target_include_directories(amigaport
    PUBLIC include
    PRIVATE src)
if(NOT MSVC)
    target_compile_options(amigaport PRIVATE
        -ffunction-sections
        -fdata-sections)
endif()
target_link_libraries(amigaport PRIVATE PUAE::M68kEmbed)

if(AMIGAPORT_BUILD_TESTS)
    enable_testing()
    add_executable(amigaport_tests
        tests/executor_test.cpp
        tests/test_support.cpp)
    target_include_directories(amigaport_tests PRIVATE tests)
    target_include_directories(amigaport_tests SYSTEM PRIVATE
        "${AMIGAPORT_PUAE_DIR}/sources/src/include")
    target_link_libraries(amigaport_tests PRIVATE amigaport)
    if(MSVC)
        # Audit final-image publics, not archive inputs: PE's COFF symbol table
        # may be empty. Keep an identity-matched linker PDB beside this image.
        target_link_options(amigaport_tests PRIVATE /DEBUG:FULL)
        set_target_properties(amigaport_tests PROPERTIES
            PDB_NAME amigaport_tests
            PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    add_test(NAME amigaport_tests COMMAND amigaport_tests)
endif()
