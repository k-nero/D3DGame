//
// Created by Thanh Nguyen on 15/8/26.
//

// eng/rhi/src/metal/metal_device.cpp — milestone 2 PARITY ONLY.
//
// Scope (enforced by review + DECISIONS.md): rainbow clear, resize, clean
// shutdown, driving an UNMODIFIED ClearSample. Everything else is
// engine_check(false && "m3"). If you are adding a PSO, a buffer, or an
// argument buffer to this file, Metal has drifted past parity — stop and go
// read DECISIONS.md.
//
// Metal-vs-D3D12 shape differences encoded below, all deliberate:
//   - no per-frame command allocator (MTL::CommandQueue recycles internally)
//   - no swap-chain array; the drawable is transient, re-acquired every frame
//   - barrier() is a no-op (automatic hazard tracking); it grows an MTL::Fence
//     body at m5, when transient aliasing forces resources into untracked heaps
//   - a clear is a render-pass LOAD ACTION, so a bare clear is an empty pass
//   - no queue-level Signal; every fence signal costs a command buffer
//
// metal-cpp rule: these three defines must appear in EXACTLY ONE .cpp in the
// whole program (they instantiate the selector/class symbols). This is that
// file.
#ifdef ENGINE_DEBUG
#define MTL_DEBUG_LAYER 1
#define MTL_SHADER_VALIDATION 1
#endif

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <engine/core/log.h>
#include <engine/core/pool.h>
#include <engine/rhi/rhi.h>

#include "engine/core/asserts.h"

#include <algorithm>
#include <cstdio>

// NOTE: no `using engine::pool::Handle;` here, unlike the D3D12 TU. The macOS
// SDK's MacTypes.h (dragged in by CoreGraphics, via CAMetalLayer.hpp) does
// `typedef Ptr* Handle;` at global scope, and the using-declaration collides
// with it. Qualify pool::Handle / pool::Pool instead. Same reason `Point` and
// `Rect` are landmines in this file.

namespace engine::rhi {
    namespace {
        // NS::String::string() is autoreleased, so every call site below has to
        // already be inside a pool. All of them are.
        NS::String *ns(const char *s) {
            return NS::String::string(s, NS::UTF8StringEncoding);
        }

        // ============================================================ resources
        struct MetalTexture {
            NS::SharedPtr<MTL::Texture> mtl;
            uint32_t width = 0, height = 0;
        };

        template<class RhiH, class T>
        RhiH to_rhi(pool::Handle<T> h) { return RhiH{.index = h.index, .gen = h.gen}; }

        template<class T, class RhiH>
        pool::Handle<T> to_pool(RhiH h) {
            return pool::Handle<T>{.index = h.index, .gen = h.gen};
        }

        class MetalDevice;

        // ========================================================= command list
        class MetalCommandList final : public ICommandList {
        public:
            // No allocator to reset — MTL::CommandQueue recycles internally, so unlike
            // D3D12 there is no per-frame CommandAllocator in FrameSlot. begin_frame
            // just re-points this at the new command buffer.
            void init(MetalDevice *dev, MTL::CommandBuffer *cmd) {
                dev_ = dev;
                cmd_ = cmd;
            }

            void barrier(std::span<const TextureBarrier>,
                         std::span<const BufferBarrier>) override {
                // Intentionally empty, and CORRECT — not a stub. Metal hazard-tracks
                // every resource not allocated from an Untracked MTL::Heap, and the
                // drawable's layout is CoreAnimation's business — Present/RenderTarget
                // transitions have no Metal spelling. This grows an MTL::Fence body at
                // m5, when transient aliasing forces resources into untracked heaps.
                // Not before.
            }

            void clear_render_target(TextureHandle h, std::array<float, 4> rgba) override;

            void set_render_targets(std::span<const TextureHandle>, TextureHandle) override {
                // Metal has no "set targets" state: targets are baked into the
                // MTL::RenderPassDescriptor at encoder creation. The real impl
                // (cache the descriptor, open the encoder lazily at first draw) is
                // m3's problem — ClearSample never calls this.
                engine_check(false && "m3");
            }

            void set_viewport_scissor(const uint32_t w, const uint32_t h) override {
                // Metal viewport is an ENCODER call, and m2 opens no long-lived encoder.
                // Cache it; m3 applies it right after renderCommandEncoder().
                vp_w_ = w;
                vp_h_ = h;
            }

            // ---- milestone 3 ----
            void set_pso(PSOHandle) override { engine_check(false && "m3"); }
            void set_index_buffer(BufferHandle, Format) override { engine_check(false && "m3"); }
            void push_constants(const void *, uint32_t) override { engine_check(false && "m3"); }
            void draw(uint32_t, uint32_t) override { engine_check(false && "m3"); }
            void draw_indexed(uint32_t, uint32_t) override { engine_check(false && "m3"); }
            void dispatch(uint32_t, uint32_t, uint32_t) override { engine_check(false && "m3"); }

        private:
            MetalDevice *dev_ = nullptr;
            MTL::CommandBuffer *cmd_ = nullptr; // borrowed, autoreleased, frame-lifetime
            uint32_t vp_w_ = 0, vp_h_ = 0;
        };

        // =============================================================== device
        class MetalDevice final : public IDevice {
        public:
            explicit MetalDevice(const DeviceDesc &desc) : desc_(desc) {
                engine_check(desc.frames_in_flight >= 1 &&
                    desc.frames_in_flight <= kMaxFramesInFlight);

                // Cocoa autorelease: device_->name() and queue_->commandBuffer() return
                // AUTORELEASED objects. app_macos.mm pumps events manually and never calls
                // [NSApp run], so there is no run-loop pool to catch them here. Own one.
                const NS::SharedPtr<NS::AutoreleasePool> pool =
                        NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
                // ---------- §1: device ----------
                // Create*/new*/alloc-init results are OWNED (+1) -> TransferPtr.
                // Anything else is autoreleased -> RetainPtr, or don't store it.
                device_ = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
                engine_check(device_.get() && "Metal device not found");

                std::snprintf(caps_.adapter_name, sizeof(caps_.adapter_name), "%s",
                              device_->name()->utf8String());
                caps_.vram_bytes = device_->recommendedMaxWorkingSetSize();
                log::info("metal: device '{}', {} MB working set",
                          caps_.adapter_name, caps_.vram_bytes / (1024 * 1024));

                // No debug-layer call here on purpose: Metal validation is
                // MTL_DEBUG_LAYER=1 / MTL_SHADER_VALIDATION=1, read when the Metal
                // framework initializes. desc.enable_debug has no API equivalent.
                if (desc.enable_debug)
                    log::info("metal: validation comes from MTL_DEBUG_LAYER=1 in the "
                        "environment — there is no API equivalent of the D3D12 "
                        "debug layer");

                // ---------- §2: queue + fence ----------
                queue_ = NS::TransferPtr(device_->newCommandQueue());
                engine_check(queue_.get());
                queue_->setLabel(ns("eng.main"));

                // The ID3D12Fence analog. SharedEvent, not Event, because begin_frame
                // needs a CPU-side wait. There is no separate OS event object on
                // Metal — the SharedEvent *is* both the fence and the waitable handle.
                fence_ = NS::TransferPtr(device_->newSharedEvent());
                engine_check(fence_.get());
                fence_->setLabel(ns("eng.frame_fence"));
                fence_->setSignaledValue(0);

                // ---------- §3: the CAMetalLayer (our "swap chain") ----------
                engine_check(desc.native_window &&
                    "Metal backend needs the CAMetalLayer* from Window::native_handle()");
                // Owned by MacWindowState/ARC; we hold a reference for our lifetime.
                layer_ = NS::RetainPtr(static_cast<CA::MetalLayer *>(desc.native_window));
                layer_->setDevice(device_.get());
                layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm); // = Format::BGRA8_UNorm,
                                                                    // matches app_macos.mm
                layer_->setFramebufferOnly(true); // no sampling/readback of the drawable at m2
                // Window::width()/height() already report PIXELS (points x
                // backingScaleFactor), so this is the drawable size verbatim.
                // Multiplying by the scale AGAIN here is the classic Retina bug.
                layer_->setDrawableSize(CGSizeMake(desc.width, desc.height));
                // Only 2 and 3 are legal values.
                layer_->setMaximumDrawableCount(std::clamp(desc.frames_in_flight, 2u, 3u));

                // ---------- §4: the one backbuffer slot ----------
                // Metal's drawable is TRANSIENT: a fresh one per frame, no stable
                // identity, valid only until commit. So instead of D3D12's N acquired
                // backbuffers we reserve ONE pool slot and swap its payload each frame —
                // the handle (index+gen) stays stable, only the MTL::Texture* rotates.
                backbuffer_ = to_rhi<TextureHandle>(textures_.emplace());
            }

            ~MetalDevice() override {
                // destroy_device already ran wait_idle.
                textures_.destroy(to_pool<MetalTexture>(backbuffer_));
            }

            // ================================================== frame loop
            FrameContext begin_frame() override {
                engine_check(!in_frame_);
                in_frame_ = true;

                // nextDrawable, commandBuffer, RenderPassDescriptor and encoders are
                // ALL autoreleased. Without a pool per frame the drawable pool never
                // drains and the loop wedges after ~3 frames.
                frame_pool_ = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

                const auto idx =
                        static_cast<uint32_t>(frame_counter_ % desc_.frames_in_flight);

                // THE wait: this slot's previous submission must be fully consumed
                // before we hand its resources out again. Same single line as D3D12,
                // different primitive underneath.
                wait_fence(frame_[idx].fence_value);

                cmd_buffer_ = queue_->commandBuffer(); // autoreleased
                engine_check(cmd_buffer_);
                cmd_buffer_->setLabel(ns("eng.frame"));
                cmd_.init(this, cmd_buffer_);

                // +0 autoreleased, and it must outlive commit() -> RetainPtr.
                drawable_ = NS::RetainPtr(layer_->nextDrawable());
                engine_check(drawable_.get() && "nextDrawable returned nil (timeout?)");

                MetalTexture *bb = texture(backbuffer_);
                bb->mtl = NS::RetainPtr(drawable_->texture());
                bb->width = desc_.width;
                bb->height = desc_.height;

                return FrameContext{
                    .cmd = &cmd_,
                    .backbuffer = backbuffer_,
                    .frame_index = idx,
                };
            }

            void end_frame() override {
                engine_check(in_frame_);
                in_frame_ = false;

                // D3D12: Execute -> Present -> Signal, three separate queue calls.
                // Metal: all three are encoded into ONE command buffer, then committed.
                // Order inside the buffer is what "signal after present" means here.
                cmd_buffer_->presentDrawable(drawable_.get());

                const uint64_t v = ++fence_value_;
                cmd_buffer_->encodeSignalEvent(fence_.get(), v);
                cmd_buffer_->commit();

                frame_[frame_counter_ % desc_.frames_in_flight].fence_value = v;
                ++frame_counter_;

                // Release this frame's drawable BEFORE draining the pool. Holding it
                // past here is the three-frame-freeze bug.
                texture(backbuffer_)->mtl.reset();
                drawable_.reset();
                cmd_buffer_ = nullptr;
                cmd_.init(nullptr, nullptr);
                frame_pool_.reset();
            }

            void resize(const uint32_t w, const uint32_t h) override {
                engine_check(!in_frame_);
                if (w == 0 || h == 0) return; // minimized
                if (w == desc_.width && h == desc_.height) return;
                desc_.width = w;
                desc_.height = h;

                // No ResizeBuffers equivalent and nothing to re-acquire — the drawable
                // is fetched fresh every frame. Still wait: an in-flight frame is
                // rendering into a drawable sized the old way.
                wait_idle();
                layer_->setDrawableSize(CGSizeMake(w, h));
                log::info("metal: resized to {}x{}", w, h);
            }

            void wait_idle() override {
                const NS::SharedPtr<NS::AutoreleasePool> pool =
                        NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

                // No queue->Signal(fence, v) on Metal: a signal must be ENCODED, so an
                // idle-wait costs one empty command buffer.
                const uint64_t v = ++fence_value_;
                MTL::CommandBuffer *cmd = queue_->commandBuffer(); // autoreleased
                engine_check(cmd);
                cmd->setLabel(ns("eng.wait_idle"));
                cmd->encodeSignalEvent(fence_.get(), v);
                cmd->commit();
                wait_fence(v);
            }

            // ================================================== resources (m3)
            BufferHandle create_buffer(const BufferDesc &) override {
                engine_check(false && "m3");
                return {};
            }

            TextureHandle create_texture(const TextureDesc &) override {
                engine_check(false && "m3");
                return {};
            }

            PSOHandle create_graphics_pso(const GraphicsPSODesc &) override {
                engine_check(false && "m3");
                return {};
            }

            void destroy(BufferHandle) override { engine_check(false && "m3"); }

            void destroy(TextureHandle) override {
                // Only the backbuffer slot exists today, and begin_frame/teardown own
                // it — forbid external destroy.
                engine_check(false && "m3");
            }

            void destroy(PSOHandle) override { engine_check(false && "m3"); }

            uint32_t bindless_index(BufferHandle) override {
                engine_check(false && "m3");
                return 0;
            }

            uint32_t bindless_index(TextureHandle) override {
                engine_check(false && "m3");
                return 0;
            }

            void *map(BufferHandle) override {
                engine_check(false && "m3");
                return nullptr;
            }

            void unmap(BufferHandle) override { engine_check(false && "m3"); }

            [[nodiscard]] const DeviceCaps &caps() const override { return caps_; }

            // ================================================== internals
            MetalTexture *texture(const TextureHandle h) {
                MetalTexture *t = textures_.get(to_pool<MetalTexture>(h));
                engine_check(t && "stale TextureHandle");
                return t;
            }

        private:
            struct FrameSlot {
                uint64_t fence_value = 0; // 0 => never submitted: no wait
            };

            // wait_fence: fence_->GetCompletedValue() -> signaledValue()
            void wait_fence(const uint64_t value) const {
                if (value == 0 || fence_->signaledValue() >= value) return;
                // timeout is MILLISECONDS, not ns/ticks. Returns false on timeout.
                const bool ok = fence_->waitUntilSignaledValue(value, ~0ull);
                engine_check(ok && "GPU fence wait timed out");
            }

            DeviceDesc desc_;
            DeviceCaps caps_{};

            NS::SharedPtr<MTL::Device> device_;
            NS::SharedPtr<MTL::CommandQueue> queue_;
            NS::SharedPtr<MTL::SharedEvent> fence_;
            NS::SharedPtr<CA::MetalLayer> layer_;

            uint64_t fence_value_ = 0;
            std::array<FrameSlot, kMaxFramesInFlight> frame_;
            uint64_t frame_counter_ = 0;
            bool in_frame_ = false;

            // per-frame state, torn down in end_frame
            NS::SharedPtr<NS::AutoreleasePool> frame_pool_;
            NS::SharedPtr<CA::MetalDrawable> drawable_;
            MTL::CommandBuffer *cmd_buffer_ = nullptr; // kept alive by frame_pool_
            MetalCommandList cmd_;

            pool::Pool<MetalTexture> textures_;
            TextureHandle backbuffer_{};
        };

        // ============================================ command list (out-of-line)
        void MetalCommandList::clear_render_target(const TextureHandle h,
                                                   const std::array<float, 4> rgba) {
            engine_check(cmd_ && dev_ && "clear outside begin_frame/end_frame");

            // D3D12 has ClearRenderTargetView as a standalone command. Metal does not:
            // a clear is a load action, so a bare clear is an EMPTY RENDER PASS. On
            // TBDR that is a real tile pass — folding the clear into the next pass's
            // load action is deferral, which is render-graph work (m5). Not here.
            //
            // Autoreleased. Lives on the per-frame pool that begin_frame opened.
            const MTL::RenderPassDescriptor *pass = MTL::RenderPassDescriptor::renderPassDescriptor();

            MTL::RenderPassColorAttachmentDescriptor *c0 = pass->colorAttachments()->object(0);
            c0->setTexture(dev_->texture(h)->mtl.get());
            c0->setLoadAction(MTL::LoadActionClear);
            c0->setStoreAction(MTL::StoreActionStore); // set BOTH explicitly; don't
                                                       // lean on descriptor defaults
            c0->setClearColor(MTL::ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]));

            MTL::RenderCommandEncoder *enc = cmd_->renderCommandEncoder(pass);
            engine_check(enc);
            enc->setLabel(ns("clear"));
            // endEncoding is MANDATORY: Metal allows exactly one open encoder per
            // command buffer, and opening a second while one is live is a hard
            // validation failure, not a warning.
            enc->endEncoding(); // empty pass on purpose: the load action IS the work
        }
    } // namespace

    IDevice *create_metal_device(const DeviceDesc &desc) {
        return new MetalDevice(desc);
    }
} // namespace engine::rhi
