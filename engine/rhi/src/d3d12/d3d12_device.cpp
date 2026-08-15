// eng/rhi/src/d3d12/d3d12_device.cpp — milestone 2, sections 1+2:
// debug arsenal -> adapter -> device -> feature asserts -> info-queue
// callback -> fence + wait_idle. Frame loop and resources are stubbed
// with engine_check(false) and arrive in the next sections.
#include "d3d12_common.h"

#include <engine/rhi/rhi.h>

#include <cstring>

namespace engine::rhi {

namespace {

using d3d12::ComPtr;

class D3D12Device final : public IDevice {
public:
    explicit D3D12Device(const DeviceDesc& desc) : desc_(desc) {
        // ---- 1. debug arsenal — ALL of it before D3D12CreateDevice ----
        if (desc.enable_debug) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                ComPtr<ID3D12Debug1> debug1;
                if (SUCCEEDED(debug.As(&debug1)))
                    debug1->SetEnableGPUBasedValidation(TRUE);
            } else {
                log::warn("d3d12: debug layer unavailable (Graphics Tools "
                         "feature not installed?)");
            }

            // DRED: breadcrumbs + page faults, so device-removed has a story.
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }

        // ---- factory + adapter (performance-first, skip software) ----
        UINT factory_flags = desc.enable_debug ? DXGI_CREATE_FACTORY_DEBUG : 0;
        ENG_HR(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_)));

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             factory_->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i) {
            DXGI_ADAPTER_DESC1 ad{};
            adapter->GetDesc1(&ad);
            if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;                                  // the laptop-iGPU trap's cousin
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                            IID_PPV_ARGS(&device_)))) {
                d3d12::wide_to_utf8(ad.Description, caps_.adapter_name,
                                    sizeof(caps_.adapter_name));
                caps_.vram_bytes = ad.DedicatedVideoMemory;
                break;
            }
            adapter.Reset();
        }
        engine_check(device_ && "no D3D12 feature-level 12.0 hardware adapter found");
        log_info("d3d12: adapter '{}', {} MB VRAM", caps_.adapter_name,
                 caps_.vram_bytes / (1024 * 1024));

        // ---- feature asserts: fail LOUDLY at startup, not mysteriously later ----
        {
            D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
            ENG_HR(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
                                                &sm, sizeof(sm)));
            engine_check(sm.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
                  "SM 6.6 required (bindless). On the Agility runtime? "
                  "Check the D3D12/ folder next to the exe.");

            D3D12_FEATURE_DATA_D3D12_OPTIONS opts{};
            ENG_HR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                                &opts, sizeof(opts)));
            engine_check(opts.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3 &&
                  "binding tier 3 required (bindless)");

            D3D12_FEATURE_DATA_D3D12_OPTIONS12 opts12{};
            ENG_HR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
                                                &opts12, sizeof(opts12)));
            engine_check(opts12.EnhancedBarriersSupported &&
                  "enhanced barriers required — update GPU driver / Agility SDK");
        }

        // ---- validation output -> our logger; errors become breakpoints ----
        if (desc.enable_debug) {
            ComPtr<ID3D12InfoQueue1> iq;
            if (SUCCEEDED(device_.As(&iq))) {
                ENG_HR(iq->RegisterMessageCallback(
                    &D3D12Device::on_debug_message,
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &iq_cookie_));
            } else {
                log_warn("d3d12: ID3D12InfoQueue1 unavailable; validation "
                         "goes to the VS output window only");
            }
        }

        // ---- 2. the fence: the engine's one synchronization primitive ----
        ENG_HR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    IID_PPV_ARGS(&fence_)));
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        engine_check(fence_event_);

        ENG_HR(device_->CreateCommandQueue(
            &queue_desc_, IID_PPV_ARGS(&queue_)));   // direct queue, milestone-wide
    }

    ~D3D12Device() override {
        // wait_idle happened in destroy_device; report anything still alive.
        if (fence_event_) CloseHandle(fence_event_);
        // Live-object report happens after our ComPtrs release — see
        // report_live_objects() called from destroy path notes below.
    }

    void wait_idle() override {
        // Signal a fresh value and block until the GPU reaches it.
        const uint64_t v = ++fence_value_;
        ENG_HR(queue_->Signal(fence_.Get(), v));
        wait_fence(v);
    }

    // ---- everything below arrives in sections 3-5 ----
    FrameContext begin_frame() override { engine_check(false && "m2 §3"); return {}; }
    void end_frame() override           { engine_check(false && "m2 §3"); }
    void resize(uint32_t, uint32_t) override { engine_check(false && "m2 §7"); }

    BufferHandle  create_buffer(const BufferDesc&) override   { engine_check(false && "m3"); return {}; }
    TextureHandle create_texture(const TextureDesc&) override { engine_check(false && "m3"); return {}; }
    PSOHandle     create_graphics_pso(const GraphicsPSODesc&) override { engine_check(false && "m3"); return {}; }
    void destroy(BufferHandle) override  { engine_check(false && "m3"); }
    void destroy(TextureHandle) override { engine_check(false && "m3"); }
    void destroy(PSOHandle) override     { engine_check(false && "m3"); }
    uint32_t bindless_index(BufferHandle) override  { engine_check(false && "m3"); return 0; }
    uint32_t bindless_index(TextureHandle) override { engine_check(false && "m3"); return 0; }
    void* map(BufferHandle) override   { engine_check(false && "m3"); return nullptr; }
    void unmap(BufferHandle) override  { engine_check(false && "m3"); }

    const DeviceCaps& caps() const override { return caps_; }

private:
    void wait_fence(uint64_t value) {
        if (fence_->GetCompletedValue() >= value)
            return;                                    // already there — no syscall
        ENG_HR(fence_->SetEventOnCompletion(value, fence_event_));
        WaitForSingleObject(fence_event_, INFINITE);
    }

    static void CALLBACK on_debug_message(D3D12_MESSAGE_CATEGORY,
                                          D3D12_MESSAGE_SEVERITY severity,
                                          D3D12_MESSAGE_ID,
                                          LPCSTR description, void*) {
        switch (severity) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            log_error("d3d12 validation: {}", description);
            engine_check(false && "D3D12 validation error — see log");
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            log_warn("d3d12 validation: {}", description);
            break;
        default:
            log_info("d3d12: {}", description);
            break;
        }
    }

    DeviceDesc desc_;
    DeviceCaps caps_{};

    ComPtr<IDXGIFactory6>       factory_;
    ComPtr<ID3D12Device10>      device_;      // Device10: enhanced-barrier era
    ComPtr<ID3D12CommandQueue>  queue_;
    ComPtr<ID3D12Fence>         fence_;
    HANDLE                      fence_event_ = nullptr;
    uint64_t                    fence_value_ = 0;
    DWORD                       iq_cookie_   = 0;

    static constexpr D3D12_COMMAND_QUEUE_DESC queue_desc_{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
};

} // namespace

IDevice* create_d3d12_device(const DeviceDesc& desc) {
    return new D3D12Device(desc);
}

// Shutdown hygiene: call AFTER the device is deleted, so only true leaks show.
void d3d12_report_live_objects() {
#ifdef ENG_DEBUG
    d3d12::ComPtr<IDXGIDebug1> dxgi_debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug)))) {
        dxgi_debug->ReportLiveObjects(
            DXGI_DEBUG_ALL,
            DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL |
                                 DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    }
#endif
}

} // namespace eng::rhi
