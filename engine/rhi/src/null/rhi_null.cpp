//
// Created by Thanh Nguyen on 15/8/26.
//
// engine/rhi/src/null/rhi_null.cpp — the Null backend.
//
// Three permanent jobs (see DECISIONS.md): GPU-free device for tests,
// the Mac dev loop, and the layering canary. Records all command-list
// calls per frame for inspection via engine/rhi/rhi_null.h.
#include <engine/rhi/rhi.h>
#include <engine/rhi/rhi_null.h>

#include <engine/core/asserts.h>
#include <engine/core/pool.h>

#include <vector>

namespace engine::rhi {
    namespace {
        // ---- pooled resource payloads (what the "GPU" remembers about each) ----
        struct NullBuffer {
            uint64_t size = 0;
            Memory memory = Memory::GpuOnly;
            BufferUsage usage = BufferUsage::None;
            std::vector<std::byte> storage; // backs initial_data and map()
        };

        struct NullTexture {
            uint32_t width = 0, height = 0;
            Format format = Format::Unknown;
            TextureUsage usage = TextureUsage::None;
        };

        struct NullPSO {
            Format depth_format = Format::Unknown;
        };

        // eng::Handle<T> and GpuHandle<Tag> share the 24/8 layout by design —
        // these two functions are the entire conversion story.
        template<class RhiHandle, class T>
        RhiHandle to_rhi(pool::Handle<T> h) { return RhiHandle{.index = h.index, .gen = h.gen}; }

        template<class T, class RhiHandle>
        pool::Handle<T> to_pool(RhiHandle h) { return pool::Handle<T>{.index = h.index, .gen = h.gen}; }

        // ---- command list: records instead of rendering ----
        class NullCommandList final : public ICommandList {
        public:
            explicit NullCommandList(std::vector<null::Record> &out) : rec_(out) {
            }

            void barrier(const std::span<const TextureBarrier> textures,
                         const std::span<const BufferBarrier> buffers) override {
                for (const auto &t: textures) rec_.emplace_back(null::BarrierRec{t});
                for (const auto &b: buffers) rec_.emplace_back(null::BufferBarrierRec{b});
            }

            void clear_render_target(const TextureHandle t, const std::array<float, 4> rgba) override {
                rec_.emplace_back(null::ClearRec{.target = t, .color = rgba});
            }

            void set_render_targets(std::span<const TextureHandle> colors,
                                    const TextureHandle depth) override {
                rec_.emplace_back(null::SetTargetsRec{
                    .colors = {colors.begin(), colors.end()}, .depth = depth
                });
            }

            void set_viewport_scissor(const uint32_t w, const uint32_t h) override {
                rec_.emplace_back(null::ViewportRec{.width = w, .height = h});
            }

            void set_pso(const PSOHandle p) override { rec_.emplace_back(null::SetPSORec{p}); }

            void set_index_buffer(const BufferHandle b, const Format f) override {
                rec_.emplace_back(null::SetIndexBufferRec{.buffer = b, .format = f});
            }

            void push_constants(const void *data, const uint32_t bytes) override {
                engine_check(bytes <= kPushConstantBytes);
                const auto *p = static_cast<const std::byte *>(data);
                rec_.emplace_back(null::PushConstantsRec{{p, p + bytes}});
            }

            void draw(const uint32_t vc, const uint32_t ic) override {
                rec_.emplace_back(null::DrawRec{.vertex_count = vc, .instance_count = ic});
            }

            void draw_indexed(const uint32_t nc, const uint32_t ic) override {
                rec_.emplace_back(null::DrawIndexedRec{.index_count = nc, .instance_count = ic});
            }

            void dispatch(const uint32_t x, const uint32_t y, const uint32_t z) override {
                rec_.emplace_back(null::DispatchRec{.x = x, .y = y, .z = z});
            }

        private:
            std::vector<null::Record> &rec_;
        };

        // ---- device ----
        class NullDevice final : public IDevice {
        public:
            explicit NullDevice(const DeviceDesc &desc)
                : desc_(desc), cmd_(records_) {
                engine_check(desc.frames_in_flight >= 1 && desc.frames_in_flight <= kMaxFramesInFlight);
                std::strncpy(caps_.adapter_name, "Null Device", sizeof(caps_.adapter_name) - 1);
                create_backbuffer();
            }

            // ---- frame loop ----
            FrameContext begin_frame() override {
                engine_check(!in_frame_); // begin/begin without end = bug
                in_frame_ = true;
                records_.clear(); // recording is per-frame
                const auto index =
                        static_cast<uint32_t>(frame_counter_ % desc_.frames_in_flight);
                // (a real backend fence-waits here; Null has nothing to wait for)
                return FrameContext{
                    .cmd = &cmd_, .backbuffer = backbuffer_,
                    .frame_index = index
                };
            }

            void end_frame() override {
                engine_check(in_frame_);
                in_frame_ = false;
                records_.emplace_back(null::PresentRec{frame_counter_});
                ++frame_counter_;
            }

            void resize(const uint32_t w, const uint32_t h) override {
                engine_check(!in_frame_); // same rule as D3D12 will have
                desc_.width = w;
                desc_.height = h;
                textures_.destroy(to_pool<NullTexture>(backbuffer_));
                create_backbuffer(); // new handle: stale ones must die,
            } // exactly like real ResizeBuffers

            void wait_idle() override {
            } // nothing is ever in flight

            // ---- resources ----
            BufferHandle create_buffer(const BufferDesc &d) override {
                NullBuffer b{.size = d.size, .memory = d.memory, .usage = d.usage, .storage = {}};
                if (!d.initial_data.empty()) {
                    engine_check(d.initial_data.size() <= d.size);
                    b.storage.assign(d.initial_data.begin(), d.initial_data.end());
                }
                if (d.memory == Memory::Upload || d.memory == Memory::Readback)
                    b.storage.resize(d.size); // mappable memory really exists
                return to_rhi<BufferHandle>(buffers_.create(std::move(b)));
            }

            TextureHandle create_texture(const TextureDesc &d) override {
                return to_rhi<TextureHandle>(textures_.create(NullTexture{
                    .width = d.width, .height = d.height,
                    .format = d.format, .usage = d.usage
                }));
            }

            PSOHandle create_graphics_pso(const GraphicsPSODesc &d) override {
                return to_rhi<PSOHandle>(psos_.create(NullPSO{.depth_format = d.depth_format}));
            }

            void destroy(const BufferHandle h) override { buffers_.destroy(to_pool<NullBuffer>(h)); }
            void destroy(const TextureHandle h) override { textures_.destroy(to_pool<NullTexture>(h)); }
            void destroy(const PSOHandle h) override { psos_.destroy(to_pool<NullPSO>(h)); }

            // Stable per-resource integer; the pool index is exactly that.
            uint32_t bindless_index(const BufferHandle h) override {
                engine_check(buffers_.get(to_pool<NullBuffer>(h))); // stale handle = bug
                return h.index;
            }

            uint32_t bindless_index(const TextureHandle h) override {
                engine_check(textures_.get(to_pool<NullTexture>(h)));
                return h.index;
            }

            void *map(const BufferHandle h) override {
                NullBuffer *b = buffers_.get(to_pool<NullBuffer>(h));
                engine_check(b);
                engine_check(b->memory != Memory::GpuOnly); // same rule the real backend enforces
                return b->storage.data();
            }

            void unmap(BufferHandle) override {
            }

            [[nodiscard]] const DeviceCaps &caps() const override { return caps_; }

            // test-inspection hook (see rhi_null.h)
            [[nodiscard]] std::span<const null::Record> recording() const { return records_; }

        private:
            void create_backbuffer() {
                backbuffer_ = to_rhi<TextureHandle>(textures_.create(NullTexture{
                    .width = desc_.width, .height = desc_.height,
                    .format = Format::BGRA8_UNorm,
                    .usage = TextureUsage::RenderTarget
                }));
            }

            DeviceDesc desc_;
            DeviceCaps caps_{};

            pool::Pool<NullBuffer> buffers_;
            pool::Pool<NullTexture> textures_;
            pool::Pool<NullPSO> psos_;

            TextureHandle backbuffer_;
            uint64_t frame_counter_ = 0;
            bool in_frame_ = false;

            std::vector<null::Record> records_;
            NullCommandList cmd_;
        };
    } // namespace

    IDevice *create_null_device(const DeviceDesc &desc) { return new NullDevice(desc); }

    namespace null {
        std::span<const Record> recording(const IDevice *dev) {
            // Valid only for Null devices — documented contract of this header.
            return dynamic_cast<const NullDevice *>(dev)->recording();
        }
    } // namespace null
} // namespace eng::rhi
