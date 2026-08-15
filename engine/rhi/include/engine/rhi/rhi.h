//
// Created by Thanh Nguyen on 15/8/26.
//

// RULES OF THIS FILE (the layering depends on them):
//   - NO platform types. No d3d12.h, no Metal, no windows.h. Ever.
//     Backends implement this privately in rhi/src/<backend>/.
//   - Resources are generational handles, never pointers. COM/ComPtr and
//     MTL::* stay below this line.
//   - EXTRACT, DON'T GUESS: methods exist because the frame loop, the
//     render graph, or the renderer needed them. When something is missing,
//     add it from a real call site — do not speculate.
//   - Barriers are ENHANCED-BARRIER shaped (sync + access + layout),
//     matching the D3D12 model we chose. The Null backend ignores them;
//     later it records them so graph tests can assert ordering on the Mac.
//
// Milestone map:  frame loop + barriers + clear  -> milestone 2
//                 resources / PSOs / bindless    -> milestone 3

#pragma once

#ifndef ENGINE_RHI_H
#define ENGINE_RHI_H

#include <array>
#include <cstddef>
#include <span>

namespace engine::rhi {
    // ---------------------------------------------------------------- constants
    inline constexpr uint32_t kMaxFramesInFlight = 3;
    inline constexpr uint32_t kMaxColorTargets = 8;
    inline constexpr uint32_t kPushConstantBytes = 128; // fixed root-constant budget,
    // part of the global bindless
    // root signature contract

    // ------------------------------------------------------------------ handles
    // Same 24/8 generational layout as eng::Handle, restated here so rhi.h has
    // zero dependencies on core containers. The Tag makes BufferHandle and
    // TextureHandle distinct types — they cannot be swapped at a call site.
    template<class Tag>
    struct GpuHandle {
        uint32_t index: 24 = 0;
        uint32_t gen: 8 = 0; // gen 0 = null, same convention as pool.h

        [[nodiscard]] bool is_null() const { return gen == 0; }

        bool operator==(const GpuHandle &) const = default;
    };

    using BufferHandle = GpuHandle<struct BufferTag>;
    using TextureHandle = GpuHandle<struct TextureTag>;
    using PSOHandle = GpuHandle<struct PSOTag>;

    // ------------------------------------------------------------------ formats
    enum class Format : uint8_t {
        Unknown,
        RGBA8_UNorm,
        BGRA8_UNorm, // typical swapchain format
        RGBA16_Float,
        RGBA32_Float,
        R16_UInt, // index buffers
        R32_UInt, // index buffers
        D32_Float,
    };

    // -------------------------------------------------------------------- flags
    // enum-class flags need their operators spelled out once:
#define ENG_RHI_FLAG_OPS(E)                                                   \
    constexpr E operator|(E a, E b) {                                         \
        return E(uint32_t(a) | uint32_t(b));                                  \
    }                                                                         \
    constexpr E operator&(E a, E b) {                                         \
        return E(uint32_t(a) & uint32_t(b));                                  \
    }                                                                         \
    constexpr bool any(E a) { return uint32_t(a) != 0; }

    enum class BufferUsage : uint8_t {
        None = 0,
        Vertex = 1 << 0, // NOTE: bindless vertex *pulling* wants Storage, not this;
        Index = 1 << 1, //       Vertex exists for tooling/interop completeness
        Constant = 1 << 2,
        Storage = 1 << 3,
    };

    ENG_RHI_FLAG_OPS(BufferUsage)

    enum class TextureUsage : uint8_t {
        None = 0,
        Sampled = 1 << 0,
        RenderTarget = 1 << 1,
        DepthStencil = 1 << 2,
        Storage = 1 << 3,
    };

    ENG_RHI_FLAG_OPS(TextureUsage)

    // Where the memory lives — the D3D12 heap-type trio, portable spelling.
    enum class Memory : uint8_t {
        GpuOnly, // DEFAULT heap: fast, not CPU-visible
        Upload, // CPU-write / GPU-read; map() works only on these
        Readback, // GPU-write / CPU-read
    };

    // ----------------------------------------------------------------- barriers
    // Enhanced-barrier model: WHAT was/will be happening (sync), HOW memory is
    // accessed (access), and for textures, the physical LAYOUT.
    enum class Sync : uint16_t {
        None = 0,
        All = 1 << 0,
        Draw = 1 << 1,
        PixelShading = 1 << 2,
        RenderTarget = 1 << 3,
        DepthStencil = 1 << 4,
        Compute = 1 << 5,
        Copy = 1 << 6,
    };

    ENG_RHI_FLAG_OPS(Sync)

    enum class Access : uint16_t {
        NoAccess = 0,
        RenderTarget = 1 << 0,
        DepthWrite = 1 << 1,
        ShaderRead = 1 << 2,
        UnorderedAccess = 1 << 3,
        CopySrc = 1 << 4,
        CopyDst = 1 << 5,
    };

    ENG_RHI_FLAG_OPS(Access)

    enum class Layout : uint8_t {
        Undefined,
        Present,
        RenderTarget,
        DepthWrite,
        ShaderRead,
        UnorderedAccess,
        CopySrc,
        CopyDst,
    };

    struct TextureBarrier {
        TextureHandle texture;
        Sync sync_before = Sync::None;
        Sync sync_after = Sync::None;
        Access access_before = Access::NoAccess;
        Access access_after = Access::NoAccess;
        Layout layout_before = Layout::Undefined;
        Layout layout_after = Layout::Undefined;
    };

    struct BufferBarrier {
        BufferHandle buffer;
        Sync sync_before = Sync::None;
        Sync sync_after = Sync::None;
        Access access_before = Access::NoAccess;
        Access access_after = Access::NoAccess;
    };

    // -------------------------------------------------------- resource descs
    // sokol-style: designated initializers, zero is a sane default, adding a
    // field never breaks a caller.
    struct BufferDesc {
        uint64_t size = 0;
        Memory memory = Memory::GpuOnly;
        BufferUsage usage = BufferUsage::None;
        std::span<const std::byte> initial_data{}; // staged through the upload ring
        const char *debug_name = nullptr; // shows up in PIX / debug layer
    };

    struct TextureDesc {
        uint32_t width = 1;
        uint32_t height = 1;
        uint16_t mip_count = 1;
        Format format = Format::RGBA8_UNorm;
        TextureUsage usage = TextureUsage::Sampled;
        std::span<const std::byte> initial_data{};
        const char *debug_name = nullptr;
    };

    struct GraphicsPSODesc {
        std::span<const std::byte> vs_bytecode; // DXIL from DXC; Null ignores
        std::span<const std::byte> ps_bytecode;
        std::span<const Format> color_formats; // render-target formats, in order
        Format depth_format = Format::Unknown; // Unknown = no depth

        // minimal fixed-function; grows as real passes need more
        bool depth_test = true;
        bool depth_write = true;

        enum class Cull : uint8_t { None, Back, Front } cull = Cull::Back;

        const char *debug_name = nullptr;
    };

    // ------------------------------------------------------------ command list
    class ICommandList {
    public:
        virtual ~ICommandList() = default;

        // RULE: no default arguments on virtuals — they bind statically and
        // silently diverge across overrides. Defaults live on these non-virtual
        // convenience overloads instead:
        void barrier(const std::span<const TextureBarrier> textures) { barrier(textures, {}); }
        void draw(const uint32_t vertex_count) { draw(vertex_count, 1); }
        void draw_indexed(const uint32_t index_count) { draw_indexed(index_count, 1); }

        void set_render_targets(const std::span<const TextureHandle> colors) {
            set_render_targets(colors, {});
        }

        virtual void barrier(std::span<const TextureBarrier> textures,
                             std::span<const BufferBarrier> buffers) = 0;

        virtual void clear_render_target(TextureHandle, std::array<float, 4> rgba) = 0;

        virtual void set_render_targets(std::span<const TextureHandle> colors,
                                        TextureHandle depth) = 0;

        virtual void set_viewport_scissor(uint32_t width, uint32_t height) = 0;

        virtual void set_pso(PSOHandle) = 0;

        virtual void set_index_buffer(BufferHandle, Format index_format) = 0;

        // Bindless contract: resources reach shaders as integer indices packed
        // into these constants (see IDevice::bindless_index). Max kPushConstantBytes.
        virtual void push_constants(const void *data, uint32_t bytes) = 0;

        virtual void draw(uint32_t vertex_count, uint32_t instance_count) = 0;

        virtual void draw_indexed(uint32_t index_count, uint32_t instance_count) = 0;

        virtual void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z) = 0;
    };

    // ------------------------------------------------------------------- device
    struct DeviceCaps {
        char adapter_name[128] = "unknown";
        uint64_t vram_bytes = 0;
    };

    // Everything begin_frame hands back is valid until the matching end_frame.
    struct FrameContext {
        ICommandList *cmd = nullptr;
        TextureHandle backbuffer; // already usable as a render target
        uint32_t frame_index = 0; // 0..frames_in_flight-1
    };

    class IDevice {
    public:
        virtual ~IDevice() = default;

        // ---- frame loop (milestone 2) ----
        // begin_frame blocks until frame_index's per-frame resources are provably
        // free (the fence wait), then resets that frame's allocator + list.
        virtual FrameContext begin_frame() = 0;

        // end_frame closes, submits, signals the fence, presents.
        virtual void end_frame() = 0;

        virtual void resize(uint32_t width, uint32_t height) = 0;

        virtual void wait_idle() = 0; // fence-to-latest; shutdown & resize use it

        // ---- resources (milestone 3) ----
        [[nodiscard]] virtual BufferHandle create_buffer(const BufferDesc &) = 0;

        [[nodiscard]] virtual TextureHandle create_texture(const TextureDesc &) = 0;

        [[nodiscard]] virtual PSOHandle create_graphics_pso(const GraphicsPSODesc &) = 0;

        // Deferred internally: safe to call while the GPU still uses the resource.
        virtual void destroy(BufferHandle) = 0;

        virtual void destroy(TextureHandle) = 0;

        virtual void destroy(PSOHandle) = 0;

        // The bindless story: the integer a shader uses via ResourceDescriptorHeap[i].
        [[nodiscard]] virtual uint32_t bindless_index(BufferHandle) = 0;

        [[nodiscard]] virtual uint32_t bindless_index(TextureHandle) = 0;

        // Memory::Upload buffers only.
        [[nodiscard]] virtual void *map(BufferHandle) = 0;

        virtual void unmap(BufferHandle) = 0;

        [[nodiscard]] virtual const DeviceCaps &caps() const = 0;
    };

    // ------------------------------------------------------------------ factory
    enum class Backend : uint8_t {
        Null, // everywhere: tests, Mac development
        D3D12, // Windows
        Metal, // toolchain-proof stub only (see DECISIONS.md)
        OpenGL,
        Vulkan,
        D3D11,
    };

    struct DeviceDesc {
        void *native_window = nullptr; // HWND on Windows; Null ignores
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t frames_in_flight = 2; // <= kMaxFramesInFlight
        bool enable_debug = true; // debug layer + GPU validation + DRED
    };

    [[nodiscard]] IDevice *create_device(Backend, const DeviceDesc &);

    void destroy_device(IDevice *); // wait_idle + teardown
} // namespace eng::rhi

#endif //ENGINE_RHI_H
