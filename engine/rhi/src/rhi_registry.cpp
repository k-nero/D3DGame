// eng/rhi/src/rhi_registry.cpp — create_device dispatch.
#include <engine/rhi/rhi.h>

#include <engine/core/asserts.h>

namespace engine::rhi {

    // Each backend TU provides its factory:
    IDevice* create_null_device(const DeviceDesc&);
#ifdef ENGINE_RHI_D3D12
    IDevice* create_d3d12_device(const DeviceDesc&);
#endif
#ifdef ENGINE_RHI_METAL_STUB
    IDevice* create_metal_stub_device(const DeviceDesc&);
#endif

    IDevice* create_device(Backend backend, const DeviceDesc& desc) {
        switch (backend) {
            case Backend::Null:
                return create_null_device(desc);

            case Backend::D3D12:
#ifdef ENGINE_RHI_D3D12
                return create_d3d12_device(desc);
#else
                engine_check(false && "D3D12 backend not built on this platform");
                return nullptr;
#endif

            case Backend::Metal:
#ifdef ENGINE_RHI_METAL_STUB
                return create_metal_stub_device(desc);   // logs the real GPU, returns Null
#else
                engine_check(false && "Metal stub not built (ENGINE_RHI_METAL_STUB=OFF or non-Apple)");
                return nullptr;
            default: ;
#endif
        }
        engine_check(false && "unknown backend");
        return nullptr;
    }

    void destroy_device(IDevice* dev) {
        if (!dev) return;
        dev->wait_idle();
        delete dev;
    }

} // namespace eng::rhi