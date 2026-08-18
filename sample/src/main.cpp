// samples/m2_clear/main.cpp — milestone 2's done-when, now through
// Application: window/device/loop/resize/timing/leak-report handled by
// the base; the sample is ONLY its frame content.
#include <engine/app/application.h>

#include <cmath>

using namespace engine;

namespace {
    class ClearSample final : public app::Application {
    public:
        using Application::Application;

    protected:
        void on_frame(const rhi::FrameContext &fr, float dt) override {
            const rhi::TextureBarrier to_rt{
                .texture = fr.backbuffer,
                .sync_after = rhi::Sync::RenderTarget,
                .access_after = rhi::Access::RenderTarget,
                .layout_before = rhi::Layout::Present,
                .layout_after = rhi::Layout::RenderTarget,
            };
            fr.cmd->barrier({{to_rt}});

            t_ += dt; // time-based now, not frame-based: resize stalls don't jump the hue
            fr.cmd->clear_render_target(fr.backbuffer, {
                                            0.5f + 0.5f * std::sin(t_),
                                            0.5f + 0.5f * std::sin(t_ + 2.09f),
                                            0.5f + 0.5f * std::sin(t_ + 4.19f),
                                            1.0f,
                                        });

            const rhi::TextureBarrier to_present{
                .texture = fr.backbuffer,
                .sync_before = rhi::Sync::RenderTarget,
                .access_before = rhi::Access::RenderTarget,
                .layout_before = rhi::Layout::RenderTarget,
                .layout_after = rhi::Layout::Present,
            };
            fr.cmd->barrier({{to_present}});
        }

        float t_ = 0.f;
    };
}

int main() {
    return ClearSample({.title = "m2: clear", .width = 1280, .height = 720}).run();
}
