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
# Vulkan backend — all three platforms.
#
# The Khronos loader is only a dispatcher: it needs an ICD. On Windows
# and Linux the GPU vendor driver registers one and the loader finds it
# with no help from us. Apple ships no Vulkan driver at all, so macOS
# uses MoltenVK (Vulkan-over-Metal) as its ICD — vendored in
# engine/third_party because vcpkg has no port for it, and pointed at
# via VK_DRIVER_FILES the same way layers use VK_ADD_LAYER_PATH.
#
# NOTE this block COMPILES the backend everywhere; whether it can
# actually create a device on macOS depends on MoltenVK being present.
# Metal remains the default backend there.
# ------------------------------------------------------------------
if (WIN32 OR UNIX)
    file(GLOB ENGINE_RHI_VULKAN_SRC CONFIGURE_DEPENDS src/vulkan/*.cpp src/vulkan/*.h)
    target_sources(engine_rhi PRIVATE ${ENGINE_RHI_VULKAN_SRC})

    # GLOBAL, like the directx-* finds above: an IMPORTED target is otherwise
    # visible only in THIS directory and below, and sample/ is a sibling of
    # engine/, not a child. Without it engine_install_runtime_dependencies()
    # sees no Vulkan::Vulkan, skips staging the loader, and the packaged
    # executable dies in dyld before main().
    find_package(Vulkan REQUIRED GLOBAL)
    target_link_libraries(engine_rhi PRIVATE Vulkan::Vulkan)
    target_compile_definitions(engine_rhi PRIVATE ENGINE_RHI_VULKAN=1)

    # ------------------------------------------------------------------
    # Where vcpkg puts the validation layer MANIFESTS. Unlike the LunarG
    # installer, vcpkg does not register them with the loader — its own
    # ports/vulkan-validationlayers/usage tells you to set the environment
    # variable by hand. Baking the path in lets the backend do that for
    # itself at startup (see add_layer_search_path() in vulkan_device.cpp),
    # so validation is on by default rather than by shell ritual.
    #
    # NOT copied next to the exe: validation layers are a dev-time tool and
    # must never ship, so there is nothing to relocate. The staging-next-to-
    # the-exe path in EngineRuntime.cmake is for things that DO ship — the
    # Agility redistributables today, MoltenVK's ICD if Vulkan-on-macOS
    # ever happens.
    #
    # Layout per ports/vulkan-validationlayers/portfile.cmake. The release
    # tree on purpose: a release layer module loading into a Debug app is
    # fine (separate module, C ABI), and it is what the LunarG SDK gives you.
    # ------------------------------------------------------------------
    if(WIN32)
        set(ENGINE_VK_LAYER_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    else()
        set(ENGINE_VK_LAYER_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/vulkan/explicit_layer.d")
    endif()

    if(EXISTS "${ENGINE_VK_LAYER_DIR}")
        target_compile_definitions(engine_rhi PRIVATE
                ENGINE_VK_LAYER_PATH="${ENGINE_VK_LAYER_DIR}")
    else()
        message(STATUS
                "vulkan: no layer manifests at ${ENGINE_VK_LAYER_DIR} - validation will "
                "need VK_ADD_LAYER_PATH set by hand")
    endif()

    # ------------------------------------------------------------------
    # macOS has no Vulkan driver of its own, so the ICD is the vendored
    # MoltenVK (Vulkan-over-Metal). Same trick as the layer manifests, one
    # level down: the loader is told about the driver through a JSON
    # manifest named by VK_ADD_DRIVER_FILES — a FILE here, note, where the
    # layer variable takes a DIRECTORY.
    #
    # This bakes the source-tree path, which is right for development and
    # wrong for shipping: a distributed app needs the dylib + manifest
    # staged next to the executable and located relative to it. That is the
    # remaining piece of "self-contained", and it needs an executable-path
    # primitive the engine does not have yet.
    # ------------------------------------------------------------------
    if(APPLE)
        set(ENGINE_MOLTENVK_ICD "${ENGINE_ROOT_DIR}/third_party/MoltenVK/MoltenVK_icd.json")
        if(EXISTS "${ENGINE_MOLTENVK_ICD}")
            target_compile_definitions(engine_rhi PRIVATE
                    ENGINE_VK_ICD_FILE="${ENGINE_MOLTENVK_ICD}")
        else()
            message(STATUS
                    "vulkan: no vendored MoltenVK at ${ENGINE_MOLTENVK_ICD} - the Vulkan "
                    "backend will find no driver on macOS (see third_party/MoltenVK/VERSION.txt)")
        endif()
    endif()
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
