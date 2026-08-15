// eng/app/src/application.cpp
#include <engine/app/application.h>

#include <engine/core/asserts.h>
#include <engine/core/log.h>

#include <algorithm>
#include <chrono>

#include "engine/core/asserts.h"

#ifdef _WIN32
namespace engine::rhi {
    void d3d12_report_live_objects();
} // TODO: move decl to rhi.h
#endif

namespace engine::app {
    Application::Application(const ApplicationDesc &desc) {
        window_ = std::make_unique<Window>(WindowDesc{
            .title = desc.title, .width = desc.width, .height = desc.height
        });

        device_ = rhi::create_device(desc.backend, rhi::DeviceDesc{
                                         .native_window = window_->native_handle(),
                                         .width = window_->width(),
                                         .height = window_->height(),
                                         .enable_debug = desc.enable_debug,
                                     });
        engine_check(device_);
        log::info("app: '{}' on '{}'", desc.title, device_->caps().adapter_name);
    }

    Application::~Application() {
        rhi::destroy_device(device_); // wait_idle inside
        window_.reset();
#ifdef _WIN32
        rhi::d3d12_report_live_objects(); // zero-leaks proof, automated for every app
#endif
    }

    int Application::run() {
        on_start();

        // steady_clock is QPC-backed on Windows — the high-res timer, portably.
        using clock = std::chrono::steady_clock;
        auto last = clock::now();

        while (!quit_ && window_->pump()) {
            if (window_->consume_resize()) {
                device_->resize(window_->width(), window_->height());
                on_resize(window_->width(), window_->height());
            }

            const auto now = clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            // Clamp: a breakpoint or window drag stalls the loop for seconds;
            // without this, the first frame back gets a giant dt and physics
            // (later) explodes. 100ms cap = "pretend at worst 10 fps happened".
            dt = std::min(dt, 0.1f);

            const rhi::FrameContext fr = device_->begin_frame();
            on_frame(fr, dt);
            device_->end_frame();
        }

        on_stop();
        return 0;
    }
} // namespace eng::app
