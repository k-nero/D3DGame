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

# Same pattern, same reason: we are the engine's own workspace, so we want its
# install rules; engine/CMakeLists.txt defaults them OFF for embedders.
set(ENGINE_INSTALL ON CACHE BOOL "Generate install and export rules for the engine")

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

# ---------------------------------------------------------------------------
# Packaging (CPack).
#
# Guarded by PROJECT_IS_TOP_LEVEL for the same reason everything else in this
# file is: packaging is a property of whoever owns the build tree. An embedder
# doing add_subdirectory(<repo>/engine) must not find `package` targets and a
# CPackConfig.cmake describing OUR sample appearing in THEIR build.
#
# Components, one archive each (CPACK_ARCHIVE_COMPONENT_INSTALL):
#   runtime  the runnable sample plus the DLLs it needs. Declared in sample/.
#   sdk      headers + libengine + engine-config.cmake. NOT YET — the facade
#            links its OBJECT modules PRIVATE, so install(EXPORT) currently
#            fails with "requires target engine_core that is not in any export
#            set", and the public surface still leaks Boost::log and std::span.
#            When it lands, add it to CPACK_COMPONENTS_ALL below.
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME                "engine")
    set(CPACK_PACKAGE_VENDOR              "Nero")
    set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "D3D12-first, RHI-portable game engine")

    # CMAKE_SYSTEM_PROCESSOR, not the triplet: the archive name should say what
    # hardware it runs on, and vcpkg's triplet is a build-side concept.
    set(CPACK_PACKAGE_FILE_NAME
            "engine-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

    # Both of these are needed and they are not the same knob.
    # CPACK_PACKAGE_INSTALL_DIRECTORY is the default install location the GUI
    # installers (NSIS, IFW) offer; the archive generators ignore it entirely.
    # What stops a ZIP/TGZ from being a tarbomb is the TOPLEVEL flag, and the
    # archive generator turns it OFF by default whenever component packaging is
    # on - so with CPACK_ARCHIVE_COMPONENT_INSTALL below it must be set back
    # explicitly, or the tarball unpacks its contents straight into whatever
    # directory the user happened to be standing in.
    set(CPACK_PACKAGE_INSTALL_DIRECTORY   "engine-${PROJECT_VERSION}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY  ON)

    # Apache-2.0 section 4(a) requires a copy of the License to travel with any
    # distribution of the work, so this is a correctness item, not a nicety.
    # CPACK_RESOURCE_FILE_LICENSE is separately what the GUI installers show on
    # their "accept the terms" page - NSIS in particular insists on a .txt
    # extension, so that day this wants a configure_file() to LICENSE.txt.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            DESTINATION . COMPONENT runtime)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

    # Quote CPACK_* correctly when writing CPackConfig.cmake. Off by default
    # only for backwards compatibility; there is no reason to leave it off.
    set(CPACK_VERBATIM_VARIABLES ON)

    # Plain if(), NOT a generator expression: CPACK_GENERATOR is consumed by
    # the cpack SCRIPT reading CPackConfig.cmake, where nothing expands genexes.
    # A $<IF:...> here would be written through verbatim and match no generator.
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
    else()
        set(CPACK_GENERATOR TGZ)
    endif()

    # OFF: one archive holding both components, WITH a top-level directory.
    #
    # The alternative is a real tradeoff rather than an oversight. Turning this
    # ON yields engine-<ver>-<plat>-runtime.tar.gz and -sdk.tar.gz, which is the
    # right split by audience - a player wants neither include/ nor lib/, a
    # developer wants no sample. The price is that the archive generators force
    # CPACK_INCLUDE_TOPLEVEL_DIRECTORY off under component packaging (verified:
    # setting it back ON has no effect), so BOTH archives become tarbombs.
    # One tidy download beats two rude ones until somebody actually asks for
    # the split; flipping this single line is the whole change.
    set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
    set(CPACK_COMPONENTS_ALL runtime sdk)
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "ClearSample")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION
            "The sample application and its runtime dependencies")
    set(CPACK_COMPONENT_SDK_DISPLAY_NAME "Engine SDK")
    set(CPACK_COMPONENT_SDK_DESCRIPTION
            "Public headers, the engine library and its CMake package files")

    # MUST BE THE LAST THING IN THIS FILE. include(CPack) snapshots the CPACK_*
    # variables as they stand at this instant and writes CPackConfig.cmake from
    # them; anything set afterwards is silently ignored. That is the single
    # most common way a CPack setup quietly does the wrong thing.
    include(CPack)
endif()
