# Backend-agnostic RHI. include/engine/rhi/rhi.h has ZERO platform types;
# d3d12/, metal/ and vulkan/ live under src/ and stay private.
#
# NOTE: plain GLOB (non-recursive) on purpose — GLOB_RECURSE on src/*.cpp would
# swallow the platform backends, which must only join the target inside their
# own platform block below. This has bitten us before.
add_library(engine_rhi OBJECT)

file(GLOB ENGINE_RHI_SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)
file(GLOB_RECURSE ENGINE_RHI_PUBLIC_HEADERS CONFIGURE_DEPENDS include/*.h)
list(APPEND ENGINE_RHI_SRC ${ENGINE_RHI_PUBLIC_HEADERS})
target_sources(engine_rhi PRIVATE ${ENGINE_RHI_SRC})

target_include_directories(engine_rhi
        PUBLIC  include        # NO platform types allowed in here
        PRIVATE src)

target_link_libraries(engine_rhi PUBLIC engine_core)

# ------------------------------------------------------------------
# D3D12 backend — Windows only. This block and the two below are the
# only places in the whole build that link a graphics API.
# ------------------------------------------------------------------
if (WIN32)
    # Agility's exports must be compiled into EACH EXECUTABLE: a static lib
    # dead-strips the object because nothing references it. Hence an INTERFACE
    # target carrying the source, which every exe links.
    add_library(engine_agility_exports INTERFACE)
    add_library(engine::agility_exports ALIAS engine_agility_exports)
    target_sources(engine_agility_exports INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/d3d12_agility.cpp)
    target_link_libraries(engine_agility_exports INTERFACE
            Microsoft::DirectX12-Agility)   # so the source finds the Agility d3d12.h

    file(GLOB ENGINE_RHI_D3D12_SRC CONFIGURE_DEPENDS src/d3d12/*.cpp src/d3d12/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_D3D12_SRC})

    find_package(D3D12MemoryAllocator CONFIG REQUIRED)
    # GLOBAL: engine_stage_runtime_dependencies() reads these imported targets
    # from the *caller's* directory scope (sample/), so directory-local
    # visibility would not be enough.
    find_package(directx12-agility CONFIG REQUIRED GLOBAL)
    find_package(directx-dxc CONFIG REQUIRED GLOBAL)
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
    file(GLOB ENGINE_RHI_VULKAN_SRC CONFIGURE_DEPENDS src/vulkan/*.cpp src/vulkan/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_VULKAN_SRC})

    find_package(Vulkan REQUIRED)
    target_link_libraries(engine_rhi PRIVATE Vulkan::Vulkan)
    target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_VULKAN=1)
endif ()

# ------------------------------------------------------------------
# Metal backend — Apple only. metal-cpp is vendored (Apple zip, see
# third_party/metal-cpp/VERSION.txt) and stays a PRIVATE include dir:
# no metal-cpp type may reach a public header.
# ------------------------------------------------------------------
if (APPLE)
    file(GLOB ENGINE_RHI_METAL_SRC CONFIGURE_DEPENDS src/metal/*.cpp src/metal/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_METAL_SRC})

    # ENGINE_ROOT_DIR, not CMAKE_SOURCE_DIR: the old form hard-coded the
    # assumption that this repo is always the top-level project.
    target_include_directories(engine_rhi PRIVATE
            ${ENGINE_ROOT_DIR}/third_party/metal-cpp)
    target_link_libraries(engine_rhi PRIVATE
            "-framework Foundation" "-framework Metal" "-framework QuartzCore")
    # metal-cpp forces visibility("default") on its ~1900 selector/class symbols
    # unless this is defined, which overrides CXX_VISIBILITY_PRESET and dumps all
    # of them into libengine.dylib's export table. metal-cpp is private to this
    # module and no MTL:: type reaches a public header, so hide them.
    target_compile_definitions(engine_rhi PRIVATE
            ENGINE_RHI_METAL=1
            METALCPP_SYMBOL_VISIBILITY_HIDDEN)
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        $<TARGET_PROPERTY:engine_rhi,SOURCES>)
