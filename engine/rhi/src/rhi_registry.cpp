// eng/rhi/src/rhi_registry.cpp — create_device dispatch.
#include <engine/rhi/rhi.h>

#include <engine/core/asserts.h>

namespace engine::rhi {
#ifdef ENGINE_RHI_D3D12
    IDevice *create_d3d12_device(const DeviceDesc &);
#endif
#ifdef ENGINE_RHI_METAL
    IDevice *create_metal_device(const DeviceDesc &);
#endif
#ifdef ENGINE_RHI_VULKAN
    IDevice *create_vulkan_device(const DeviceDesc &);
#endif


    IDevice *create_device(const Backend backend, const DeviceDesc &desc) {
        switch (backend) {
            case Backend::D3D12:
#ifdef ENGINE_RHI_D3D12
                return create_d3d12_device(desc);
#else
                engine_check(false && "D3D12 backend not built on this platform");
                return nullptr;
#endif
            case Backend::Metal:
#ifdef ENGINE_RHI_METAL
                return create_metal_device(desc);
#else
                engine_check(false && "Metal backend not built (ENGINE_RHI_METAL=OFF or non-Apple)");
                return nullptr;
#endif
            case Backend::Vulkan:
#ifdef ENGINE_RHI_VULKAN
                return create_vulkan_device(desc);
#else
                engine_check(false && "Vulkan backend not built (ENGINE_RHI_VULKAN=OFF)");
                return nullptr;
#endif
            default:
                engine_check(false && "unknown backend");
                return nullptr;
        }
    }

    void destroy_device(IDevice *dev) {
        if (!dev) return;
        dev->wait_idle();
        delete dev;
    }
} // namespace eng::rhi
