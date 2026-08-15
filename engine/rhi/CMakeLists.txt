# Null backend + registry compile on every platform.
file(GLOB_RECURSE ENGINE_RHI_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/null/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_RHI_SRC})
add_library(engine_rhi STATIC ${ENGINE_RHI_SRC}
        src/null/rhi_null.cpp)

target_include_directories(engine_rhi
    PUBLIC  include        # eng/rhi/rhi.h — NO platform types allowed in here
    PRIVATE src)

target_link_libraries(engine_rhi
    PUBLIC  engine_core)

# ------------------------------------------------------------------
# D3D12 backend — Windows only. This block is the only place in the
# whole build that links graphics APIs.
# ------------------------------------------------------------------
if(WIN32)
    file(GLOB ENGINE_RHI_D3D12_SRC CONFIGURE_DEPENDS src/d3d12/*.cpp)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_D3D12_SRC})

    find_package(D3D12MemoryAllocator CONFIG REQUIRED)
    find_package(directx12-agility    CONFIG REQUIRED)
    find_package(directx-dxc          CONFIG REQUIRED)

    # NOTE: verify the imported target names against your installed port
    # versions ("vcpkg install <pkg>" prints usage, or read
    # vcpkg_installed/*/share/<pkg>/). They have shifted between versions.
    target_link_libraries(engine_rhi PRIVATE
        d3d12 dxgi dxguid
        GPUOpen::D3D12MemoryAllocator
        Microsoft::DirectX12-Agility
        Microsoft::DirectXShaderCompiler)

    target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_D3D12=1)
endif()

if(APPLE)
    option(ENGINE_RHI_METAL_STUB "Build the Metal toolchain-proof stub" ON)
    file(GLOB ENGINE_RHI_D3D12_SRC CONFIGURE_DEPENDS src/metal/*.cpp)
    if(ENGINE_RHI_METAL_STUB)
        target_sources(engine_rhi PRIVATE ${ENGINE_RHI_METAL_SRC})
        # vendored path (skip if using a vcpkg port):
        target_include_directories(engine_rhi PRIVATE
                ${CMAKE_SOURCE_DIR}/third_party/metal-cpp)
        target_link_libraries(engine_rhi PRIVATE
                "-framework Foundation" "-framework Metal" "-framework QuartzCore")
        target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_METAL_STUB=1)
    endif()
endif()
