# Null backend + registry compile on every platform.
file(GLOB ENG_RHI_SRC CONFIGURE_DEPENDS
    src/*.cpp
    src/null/*.cpp)

add_library(eng_rhi STATIC ${ENG_RHI_SRC})

target_include_directories(eng_rhi
    PUBLIC  include        # eng/rhi/rhi.h — NO platform types allowed in here
    PRIVATE src)

target_link_libraries(eng_rhi
    PUBLIC  eng_core)

# ------------------------------------------------------------------
# D3D12 backend — Windows only. This block is the only place in the
# whole build that links graphics APIs.
# ------------------------------------------------------------------
if(WIN32)
    file(GLOB ENG_RHI_D3D12_SRC CONFIGURE_DEPENDS src/d3d12/*.cpp)
    target_sources(eng_rhi PRIVATE ${ENG_RHI_D3D12_SRC})

    find_package(D3D12MemoryAllocator CONFIG REQUIRED)
    find_package(directx12-agility    CONFIG REQUIRED)
    find_package(directx-dxc          CONFIG REQUIRED)

    # NOTE: verify the imported target names against your installed port
    # versions ("vcpkg install <pkg>" prints usage, or read
    # vcpkg_installed/*/share/<pkg>/). They have shifted between versions.
    target_link_libraries(eng_rhi PRIVATE
        d3d12 dxgi dxguid
        GPUOpen::D3D12MemoryAllocator
        Microsoft::DirectX12-Agility
        Microsoft::DirectXShaderCompiler)

    target_compile_definitions(eng_rhi PRIVATE ENG_RHI_D3D12=1)
endif()
