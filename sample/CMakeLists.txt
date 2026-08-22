# ===========================================================================
# ClearSample — a CONSUMER of the engine, not a part of it.
#
# The only engine target this file may name is engine::engine, exactly as an
# out-of-tree application would. If something here ever needs to reach for
# engine_app, engine_common, or a backend detail, that is a hole in the
# engine's public surface to fix upstream, not to work around down here.
# ===========================================================================
file(GLOB_RECURSE SAMPLE_SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SAMPLE_SRC})

add_executable(sample ${SAMPLE_SRC})

# One link line. C++23, the platform defines and the public include dirs all
# arrive as usage requirements of engine::engine.
target_link_libraries(sample PRIVATE engine::engine)

# Windows runtime staging (Agility exports, D3D12Core.dll into D3D12/,
# dxil.dll, dxcompiler.dll) — engine-owned, no-op on other platforms.
engine_stage_runtime_dependencies(sample)

# Console subsystem stays in dev builds so logs are visible. Switch to
# WIN32_EXECUTABLE for release polish later.

# ---------------------------------------------------------------------------
# Packaging: the `runtime` component. See the CPack block at the bottom of the
# workspace root.
#
# DESTINATION "." — a flat layout, deliberately. This component is a "download
# it and double-click" archive, not an FHS prefix; bin/ would only add a level
# to unpack past, and D3D12Core.dll has to sit in a D3D12/ folder NEXT TO the
# executable, which is a relationship bin/ does not help express.
#
# Not MACOSX_BUNDLE yet: app_macos.mm runs a manual event loop and the target
# is a plain executable, so RUNTIME is the right keyword on all three
# platforms today. A .app bundle is a real change (Info.plist, the BUNDLE
# keyword, bundle-relative resource lookup), not a packaging flag.
# ---------------------------------------------------------------------------
install(TARGETS sample
        RUNTIME DESTINATION . COMPONENT runtime)

# The DLLs, the Agility redistributable in its D3D12/ subfolder, dxil.dll.
# No-op off Windows, exactly like its staging twin above.
engine_install_runtime_dependencies(sample . runtime)
