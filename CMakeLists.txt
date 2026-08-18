# ===========================================================================
# Workspace root. This file is a WORKSPACE, not the engine:
#   - it wires up vcpkg and the artifact layout
#   - it decides which subprojects get built
# Everything about how the engine itself compiles lives in engine/CMakeLists.txt,
# so that add_subdirectory(<repo>/engine) from a foreign project is sufficient.
#
# Layout contract:
#   engine/   the library. Everything inside is an implementation detail
#             except the single consumable target `engine::engine`.
#   sample/   a consumer. It may name engine::engine and NOTHING else from
#             the engine tree — that constraint is the whole point, and it is
#             what keeps the public surface honest.
# ===========================================================================

# Range syntax: floor 3.25 (PROJECT_IS_TOP_LEVEL 3.21, CMP0079 3.13, block() 3.25)
# but adopt policies up to 4.3 when the running CMake is that new. Pinning the
# floor at 4.3 would lock the library to a CMake barely anyone has.
cmake_minimum_required(VERSION 3.25...4.3)

# ---------------------------------------------------------------------------
# vcpkg manifest wiring MUST precede project(): the toolchain file runs during
# project() and reads these variables.
# CMAKE_CURRENT_SOURCE_DIR, never CMAKE_SOURCE_DIR — the latter would point at
# the wrong tree the day this repo is add_subdirectory()'d into a bigger one.
# ---------------------------------------------------------------------------
set(VCPKG_MANIFEST_DIR   "${CMAKE_CURRENT_SOURCE_DIR}")
set(VCPKG_INSTALLED_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed")
set(VCPKG_MANIFEST_MODE  ON)

project(engine_workspace VERSION 0.1.0 LANGUAGES C CXX)

# Samples are a property of this workspace (sample/ is a sibling of engine/),
# so the knob lives here. ENGINE_BUILD_TESTS and ENGINE_WERROR are engine knobs
# and are declared in engine/CMakeLists.txt.
option(ENGINE_BUILD_SAMPLES "Build the sample programs" ${PROJECT_IS_TOP_LEVEL})

# We are the engine's own workspace, so we want its tests; engine/CMakeLists.txt
# defaults them OFF for embedders. No FORCE: if the cache entry already exists
# because the user passed -DENGINE_BUILD_TESTS=OFF, that wins.
set(ENGINE_BUILD_TESTS ON CACHE BOOL "Build engine unit tests")

# ---------------------------------------------------------------------------
# Settings that belong to whoever owns the build tree. Guarded, because
# stomping an embedder's output layout or compile_commands.json is rude.
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)              # std=c++23, not gnu++23
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)      # clangd on the Mac side

    # One predictable place per artifact kind. The per-config variants matter
    # for multi-config generators (Visual Studio), which otherwise append
    # Debug/ Release/ subdirectories of their own.
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/lib")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/lib")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/bin")
    endforeach()

    # Unconditional, and before the add_subdirectory() that registers tests:
    # enable_testing() only takes effect in the top-level directory, and it has
    # to run whether or not ENGINE_BUILD_TESTS (declared inside engine/) is on.
    enable_testing()
endif()

add_subdirectory(engine)

if(ENGINE_BUILD_SAMPLES)
    add_subdirectory(sample)
endif()
