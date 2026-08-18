# ===========================================================================
# D3DGame — an engine shipped as a library, proven by samples and tests.
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

project(engine VERSION 0.1.0 LANGUAGES C CXX)

# PROJECT_IS_TOP_LEVEL (CMake 3.21+) is false when we are embedded in another
# build. Tests and samples are ours; an embedder wants neither by default.
option(ENGINE_BUILD_TESTS   "Build engine unit tests"    ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_BUILD_SAMPLES "Build the sample programs"  ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_WERROR        "Treat engine warnings as errors" OFF)

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
endif()

# ---------------------------------------------------------------------------
# Two INTERFACE targets, split on purpose:
#
#   engine_common    language level + config/platform defines. Safe to hand to
#                    a consumer, so the `engine` facade re-exports the same set
#                    (see engine/CMakeLists.txt).
#   engine_warnings  our warning and ISA flags. Linked PRIVATE by engine
#                    targets only — a consumer of this engine must never
#                    inherit -Wshadow or /arch:AVX2 from us.
#
# The old single `engine_common` carried both AND was defined *after* the
# add_subdirectory() calls that consumed it. Order matters for INTERFACE
# targets far more than it looks: keep this above the add_subdirectory() lines.
# ---------------------------------------------------------------------------
set(ENGINE_PUBLIC_DEFINES
        $<$<CONFIG:Debug>:ENGINE_DEBUG=1>
        $<$<PLATFORM_ID:Windows>:ENGINE_PLATFORM_WINDOWS=1>
        $<$<PLATFORM_ID:Darwin>:ENGINE_PLATFORM_MACOS=1>
        $<$<PLATFORM_ID:Linux>:ENGINE_PLATFORM_LINUX=1>
        $<$<BOOL:${BUILD_SHARED_LIBS}>:ENGINE_SHARED>)   # drives api.h dllimport

add_library(engine_common INTERFACE)
add_library(engine::common ALIAS engine_common)
target_compile_features(engine_common INTERFACE cxx_std_23)
target_compile_definitions(engine_common INTERFACE ${ENGINE_PUBLIC_DEFINES})

add_library(engine_warnings INTERFACE)
if(MSVC)
    target_compile_options(engine_warnings INTERFACE
            /W4 /permissive- /MP /utf-8 /arch:AVX2)
    target_compile_definitions(engine_warnings INTERFACE
            NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(ENGINE_WERROR)
        target_compile_options(engine_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(engine_warnings INTERFACE -Wall -Wextra -Wshadow)
    if(ENGINE_WERROR)
        target_compile_options(engine_warnings INTERFACE -Werror)
    endif()
endif()

# enable_testing() has to run in the TOP-LEVEL directory for `ctest` to work
# from the build root, and before the add_subdirectory() that registers tests.
if(ENGINE_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine)

if(ENGINE_BUILD_SAMPLES)
    add_subdirectory(sample)
endif()
