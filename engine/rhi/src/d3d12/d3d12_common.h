// eng/rhi/src/d3d12/d3d12_common.h — internals shared by the d3d12/ TUs.
// PRIVATE header: nothing above rhi/src/d3d12/ may include this.
#pragma once

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <engine/core/asserts.h>
#include <engine/core/log.h>

#ifdef ENGINE_DEBUG
#include <dxgidebug.h>
#endif

namespace engine::rhi::d3d12 {
    // The COM smart pointer (same shape as eng::Ref, COM spellings — see the
    // conversation notes: Get() to pass in, IID_PPV_ARGS(&x) to receive out,
    // As() to upgrade interface versions).
    template<class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    // Every fallible COM call goes through this. SUCCEEDED/FAILED, never ==S_OK.
#define ENGINE_HR(call)                                                       \
    do {                                                                      \
        const HRESULT hr_ = (call);                                           \
        if (FAILED(hr_)) {                                                    \
            log::error("{} failed: 0x{:08x}", #call,                          \
                             static_cast<uint32_t>(hr_));                     \
            engine_check(false);                                              \
        }                                                                     \
    } while (0)

    // WCHAR adapter description -> utf8 for our logger.
    inline void wide_to_utf8(const wchar_t *in, char *out, int out_bytes) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, in, -1, out, out_bytes,
                                          nullptr, nullptr);
        if (n <= 0 && out_bytes > 0) out[0] = '\0';
    }
} // namespace eng::rhi::d3d12
