# Null backend + registry compile on every platform.
# NOTE: plain GLOB (non-recursive) on purpose — GLOB_RECURSE on src/*.cpp
# would swallow the platform backends (src/d3d12/, src/metal/), which must
# only join the target inside their platform blocks below.
file(GLOB ENGINE_RHI_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        src/null/*.cpp
        src/null/*.h)
file(GLOB_RECURSE ENGINE_RHI_PUBLIC_HEADERS CONFIGURE_DEPENDS include/*.h)
list(APPEND ENGINE_RHI_SRC ${ENGINE_RHI_PUBLIC_HEADERS})

add_library(engine_rhi STATIC ${ENGINE_RHI_SRC})

target_include_directories(engine_rhi
        PUBLIC include        # eng/rhi/rhi.h — NO platform types allowed in here
        PRIVATE src)

target_link_libraries(engine_rhi
        PUBLIC engine_core)


# ------------------------------------------------------------------
# D3D12 backend — Windows only. This block is the only place in the
# whole build that links graphics APIs.
# ------------------------------------------------------------------
if (WIN32)

    # engine/rhi/CMakeLists.txt, inside the WIN32 block:
    add_library(engine_agility_exports INTERFACE)
    target_sources(engine_agility_exports INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/d3d12_agility.cpp)
    target_link_libraries(engine_agility_exports INTERFACE
            Microsoft::DirectX12-Agility)          # so the source finds the Agility d3d12.h

    file(GLOB ENGINE_RHI_D3D12_SRC CONFIGURE_DEPENDS
            src/d3d12/*.cpp
            src/d3d12/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_D3D12_SRC})

    find_package(D3D12MemoryAllocator CONFIG REQUIRED)
    find_package(directx12-agility CONFIG REQUIRED GLOBAL)
    find_package(directx-dxc CONFIG REQUIRED)
    find_package(directx-headers CONFIG REQUIRED)
    target_link_libraries(engine_rhi PRIVATE
            d3d12 dxgi dxguid
            GPUOpen::D3D12MemoryAllocator
            Microsoft::DirectX12-Agility
            Microsoft::DirectX-Headers
            Microsoft::DirectX-Guids
            Microsoft::DirectXShaderCompiler)
    target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_D3D12=1)
endif ()

# ------------------------------------------------------------------
# Vulkan backend — Windows + Linux, NOT macOS. The Khronos loader is
# only a dispatcher; it needs an ICD, and Apple ships no Vulkan driver.
# The only macOS ICD is MoltenVK, which vcpkg does not package — so
# vulkan-sdk-components is gated "!osx" in vcpkg.json and this block
# matches it. On Windows/Linux the GPU vendor driver registers the ICD
# and the loader finds it with no help from us.
# ------------------------------------------------------------------
if (WIN32 OR (UNIX AND NOT APPLE))
    file(GLOB ENGINE_RHI_VULKAN_SRC CONFIGURE_DEPENDS
            src/vulkan/*.cpp
            src/vulkan/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_VULKAN_SRC})

    find_package(Vulkan REQUIRED)
    target_link_libraries(engine_rhi PRIVATE Vulkan::Vulkan)
    target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_VULKAN=1)
endif ()

if (APPLE)
    file(GLOB ENGINE_RHI_METAL_SRC CONFIGURE_DEPENDS
            src/metal/*.cpp
            src/metal/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_METAL_SRC})
    # vendored path (skip if using a vcpkg port):
    target_include_directories(engine_rhi PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/metal-cpp)
    target_link_libraries(engine_rhi PRIVATE
            "-framework Foundation" "-framework Metal" "-framework QuartzCore")
    target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_METAL=1)
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        $<TARGET_PROPERTY:engine_rhi,SOURCES>)