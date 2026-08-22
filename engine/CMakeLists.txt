# ===========================================================================
# The engine library.
#
# Internal shape: one OBJECT library per module (core -> rhi -> rendergraph ->
# renderer -> scene -> assets -> app), each seeing only the layer below it.
# External shape: exactly one target, `engine::engine`.
#
# Why OBJECT libraries plus one facade: OBJECT-library object files are added
# only to the target that names them DIRECTLY — they are not propagated
# transitively. `engine` names all seven, so libengine.a/.dylib is complete;
# a consumer that links `engine` gets those objects exactly once and never
# sees the module targets. (The previous build had `sample` link engine_app
# directly, which is why it failed to link: sample got app's objects and
# nothing from core/rhi/scene.)
# ===========================================================================

# Its own cmake_minimum_required + project(), deliberately: that makes engine/
# a self-contained project rather than a fragment of this repo's root.
#   - cmake_minimum_required here means OUR policies are ours. Without it the
#     engine inherits whatever policy level the embedding project declared.
#   - project() here makes PROJECT_IS_TOP_LEVEL mean what it says. Without it,
#     a subdirectory inherits the enclosing project's value (TRUE), so an
#     embedder silently got ENGINE_BUILD_TESTS=ON and built our test suite.
cmake_minimum_required(VERSION 3.25...4.3)
project(engine VERSION 0.1.0 LANGUAGES C CXX)

# Path anchor for this library. Never CMAKE_SOURCE_DIR: that is the consumer's
# tree, not ours, the moment anybody embeds us.
set(ENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${ENGINE_ROOT_DIR}/cmake")

# ---------------------------------------------------------------------------
# Engine-owned knobs. Declared HERE, not in the workspace root, so that
# add_subdirectory(<repo>/engine) from a foreign project is enough to get a
# working engine.
#
# ENGINE_BUILD_TESTS defaults OFF because the common case for this file is
# being embedded, and an unrelated project should not inherit our test suite.
# This repo's own workspace root opts in via a non-FORCE cache set, so
# -DENGINE_BUILD_TESTS=OFF on the command line still wins. (PROJECT_IS_TOP_LEVEL
# cannot express this: it is false both for a foreign embedder AND for our own
# workspace, which does want the tests.)
# ---------------------------------------------------------------------------
option(ENGINE_BUILD_TESTS "Build engine unit tests"         OFF)
option(ENGINE_WERROR      "Treat engine warnings as errors" OFF)

# Same shape and same reasoning as ENGINE_BUILD_TESTS: OFF by default because
# the common case for this file is being embedded, and an embedder's `install`
# must not start scattering OUR headers into THEIR prefix. This repo's
# workspace root opts in with a non-FORCE cache set, so -DENGINE_INSTALL=OFF on
# the command line still wins.
option(ENGINE_INSTALL     "Generate install and export rules for the engine" OFF)

# ---------------------------------------------------------------------------
# Two INTERFACE targets, split on purpose:
#
#   engine_common    language level + config/platform defines. Safe to hand to
#                    a consumer, so the `engine` facade mirrors the same set.
#   engine_warnings  our warning and ISA flags. Linked PRIVATE by engine
#                    targets only — a consumer must never inherit -Wshadow or
#                    /arch:AVX2 from us.
#
# These live in this file rather than the workspace root because they ARE the
# engine's build identity: with them at the root, add_subdirectory(engine) from
# elsewhere silently compiled the engine at the default standard (std::span
# missing) and passed -lengine_common to the linker.
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

if(APPLE)
    enable_language(OBJCXX)   # app/src/macos/app_macos.mm — AppKit is not covered by metal-cpp
endif()

# ---------------------------------------------------------------------------
# Dependencies that leak through PUBLIC headers are found HERE, at engine
# scope, not inside a module. find_package() imported targets are visible only
# in the directory that called it and below, so a target found in core/ would
# be invisible to this file — and this file is where the facade's PUBLIC link
# interface is declared.
#   core/math.h -> <DirectXMath.h>          -> Microsoft::DirectXMath
#   core/log.h  -> <boost/log/trivial.hpp>  -> Boost::log
# Everything else (imgui, nlohmann_json, stb, cgltf, D3D12MA, DXC, Metal,
# Vulkan) is private to a module and invisible to consumers.
# ---------------------------------------------------------------------------
find_package(directxmath CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS log log_setup)

set(ENGINE_MODULES core rhi rendergraph renderer scene assets app)
foreach(mod IN LISTS ENGINE_MODULES)
    add_subdirectory(${mod})
endforeach()

# ---------------------------------------------------------------------------
# Properties every module shares, applied once here instead of copy-pasted
# into seven CMakeLists. CMP0079 (CMake 3.13+) is what lets the parent
# directory call target_link_libraries() on a subdirectory's target.
# ---------------------------------------------------------------------------
foreach(mod IN LISTS ENGINE_MODULES)
    set(tgt engine_${mod})

    target_link_libraries(${tgt}
            PUBLIC  engine_common      # std level + platform/config defines
            PRIVATE engine_warnings)   # PRIVATE: warnings must not escape the engine

    # ENGINE_BUILDING selects __declspec(dllexport) in core/api.h. Private by
    # definition: only translation units *inside* the engine export.
    target_compile_definitions(${tgt} PRIVATE ENGINE_BUILDING)

    set_target_properties(${tgt} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            # OBJECT libraries do NOT inherit PIC from the shared library they
            # are folded into, so a BUILD_SHARED_LIBS=ON build fails to link on
            # Linux/macOS without this. Set unconditionally: one code path, and
            # the cost in a static build is noise.
            POSITION_INDEPENDENT_CODE ON)
endforeach()

# ---------------------------------------------------------------------------
# The one and only static/shared boundary. Respects BUILD_SHARED_LIBS.
# ---------------------------------------------------------------------------
add_library(engine)
add_library(engine::engine ALIAS engine)   # in-tree consumers use the same name an installed one would

# PRIVATE: pulls in each module's object files and its private link deps
# (imgui, Metal frameworks, d3d12, ...) without putting the module targets in
# engine's *usage* requirements. The facade's public surface is declared
# explicitly below, so it can be reviewed in one place.
#
# $<BUILD_INTERFACE:> is what makes this facade exportable. `engine` is static,
# and CMake propagates a static library's PRIVATE deps into its
# INTERFACE_LINK_LIBRARIES as $<LINK_ONLY:...> so the consumer links them too.
# For an OBJECT library that is wrong: those objects are already INSIDE
# libengine.a. Left unwrapped, install(EXPORT) refuses the whole export set
# with "requires target engine_core that is not in any export set"; wrapped,
# the entry evaluates to the target in the build tree (objects still collected,
# verified with nm) and to nothing in the installed one, which is the truth.
foreach(mod IN LISTS ENGINE_MODULES)
    target_link_libraries(engine PRIVATE $<BUILD_INTERFACE:engine_${mod}>)
endforeach()

# Public compile surface. Mirrors engine_common rather than linking it, so the
# facade owns its interface outright and stays trivially installable later.
target_compile_features(engine PUBLIC cxx_std_23)
target_compile_definitions(engine PUBLIC ${ENGINE_PUBLIC_DEFINES})
target_link_libraries(engine PUBLIC Microsoft::DirectXMath Boost::log)

# Public headers: every module's include/ dir, collected. Modules that have no
# public headers yet (rendergraph, renderer, scene, assets) simply contribute
# nothing instead of naming a directory that does not exist.
set(ENGINE_PUBLIC_INCLUDE_DIRS "")
foreach(mod IN LISTS ENGINE_MODULES)
    if(IS_DIRECTORY "${ENGINE_ROOT_DIR}/${mod}/include")
        list(APPEND ENGINE_PUBLIC_INCLUDE_DIRS "${ENGINE_ROOT_DIR}/${mod}/include")
        target_include_directories(engine PUBLIC
                $<BUILD_INTERFACE:${ENGINE_ROOT_DIR}/${mod}/include>)
    endif()
endforeach()

# One install-side entry for all of them: every module's headers are already
# namespaced (include/engine/<mod>/*.h), so they merge into a single include
# root without collision and the consumer's #include lines do not change.
include(GNUInstallDirs)
target_include_directories(engine PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(engine PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
if(BUILD_SHARED_LIBS)
    set_target_properties(engine PROPERTIES
            VERSION   ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})

    # The public surface IS annotated with ENGINE_API now (core/api.h), so a
    # shared build links. What it is NOT yet is ABI-stable: std::span,
    # std::array and std::unique_ptr cross the boundary in rhi.h/application.h,
    # and Boost::log is a PUBLIC dependency. That makes libengine.so/.dll
    # usable only by a consumer built with the SAME compiler, standard library
    # and flags - fine for this repo's own sample, not for shipping a binary.
    #
    # So this configuration is a TEST, not a deliverable: it is what catches
    # visibility and PIC regressions early. The shipped artifact is the static
    # library; the version-independent boundary is the milestone 11 `engine_c`
    # facade exporting a flat C ABI, not this one built with a flag flipped.
    # See the Delivery section of CLAUDE.md - decided on measurement (this
    # dylib exports 505 symbols, 21 of them engine:: and 480 of them Boost).
endif()

# ---------------------------------------------------------------------------
# Offline shader compilation. Mac builds get an HLSL syntax/semantic check;
# real PSO validation is Windows work.
# ---------------------------------------------------------------------------
include(CompileShaders)
engine_add_shader_target(engine_shaders "${ENGINE_ROOT_DIR}/shaders")
if(TARGET engine_shaders)
    add_dependencies(engine engine_shaders)
endif()

# Windows runtime-DLL staging (Agility, dxil, dxcompiler) lives with the
# backend that needs it, not in the sample. See the header of the file.
include(EngineRuntime)

# ---------------------------------------------------------------------------
# Install + export: the `sdk` package component.
#
# THE INSTALLED LINK CLOSURE IS DECLARED BY HAND, DELIBERATELY.
#
# `engine` is a static archive, so it does not absorb its dependencies: every
# third-party library a module links privately still has to appear on the
# CONSUMER's link line. In the build tree CMake works that list out by walking
# into the module targets, but the $<BUILD_INTERFACE:> wrap above deletes that
# path from the INSTALLED interface — it has to, or the export set will not
# generate at all. So the closure is restated here, in the same file and the
# same spirit as the facade's public surface a few lines up: one reviewable
# place, rather than an emergent property of seven CMakeLists.
#
# Getting this list wrong is not silent. It surfaces immediately as undefined
# symbols when an out-of-tree consumer links, which is exactly how it was
# checked. Keep it in sync when a module gains a dependency.
# ---------------------------------------------------------------------------
if(ENGINE_INSTALL)
    include(CMakePackageConfigHelpers)

    set(ENGINE_INSTALL_LINK_CLOSURE
            Boost::log_setup                  # core/log.cpp
            nlohmann_json::nlohmann_json      # assets
            imgui::imgui)                     # app
    if(TARGET Vulkan::Vulkan)
        list(APPEND ENGINE_INSTALL_LINK_CLOSURE Vulkan::Vulkan)
    endif()
    if(WIN32)
        list(APPEND ENGINE_INSTALL_LINK_CLOSURE
                d3d12 dxgi dxguid
                GPUOpen::D3D12MemoryAllocator
                Microsoft::DirectX12-Agility
                Microsoft::DirectX-Headers
                Microsoft::DirectX-Guids
                Microsoft::DirectXShaderCompiler)
    endif()
    if(APPLE)
        # Frameworks are plain link flags, not targets, so they need no
        # find_dependency() on the far side.
        list(APPEND ENGINE_INSTALL_LINK_CLOSURE
                "-framework Foundation" "-framework Metal"
                "-framework QuartzCore" "-framework Cocoa")
    endif()

    # INSTALL_INTERFACE only: the build tree already resolves all of this
    # through the module targets, and naming it twice there would put every
    # library on the link line a second time.
    foreach(dep IN LISTS ENGINE_INSTALL_LINK_CLOSURE)
        target_link_libraries(engine INTERFACE $<INSTALL_INTERFACE:${dep}>)
    endforeach()

    install(TARGETS engine
            EXPORT  engineTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT sdk)

    # Modules with no public headers yet contribute nothing rather than naming
    # a directory that does not exist — same rule as the include dirs above.
    foreach(mod IN LISTS ENGINE_MODULES)
        if(IS_DIRECTORY "${ENGINE_ROOT_DIR}/${mod}/include")
            install(DIRECTORY "${ENGINE_ROOT_DIR}/${mod}/include/"
                    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                    COMPONENT sdk
                    FILES_MATCHING PATTERN "*.h")
        endif()
    endforeach()

    set(ENGINE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/engine")

    install(EXPORT engineTargets
            FILE      engineTargets.cmake
            NAMESPACE engine::                 # so the imported target is
            DESTINATION ${ENGINE_INSTALL_CMAKEDIR}   # engine::engine, the same
            COMPONENT sdk)                     # name in-tree consumers already use

    configure_package_config_file(
            "${ENGINE_ROOT_DIR}/cmake/engine-config.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/engine-config.cmake"
            INSTALL_DESTINATION ${ENGINE_INSTALL_CMAKEDIR})

    # SameMinorVersion, not SameMajorVersion: at 0.x semver treats the MINOR
    # component as the breaking one, and every version this library will have
    # for a while is 0.x. Switch to SameMajorVersion at 1.0.
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/engine-config-version.cmake"
            VERSION       ${PROJECT_VERSION}
            COMPATIBILITY SameMinorVersion)

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/engine-config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/engine-config-version.cmake"
            "${ENGINE_ROOT_DIR}/cmake/EngineRuntime.cmake"   # config file includes it
            DESTINATION ${ENGINE_INSTALL_CMAKEDIR}
            COMPONENT sdk)
endif()

if(ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
