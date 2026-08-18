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
