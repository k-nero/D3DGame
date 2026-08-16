//
// Created by Thanh Nguyen on 16/8/26.
//
// eng/app/include/eng/app/application.h — the engine-owns-the-loop base
// (axis 2 decision). Samples/games derive, override the hooks, and main()
// becomes two lines. The loop calls, per frame:
//
//   pump -> resize? -> dt -> begin_frame -> on_frame(ctx, dt) -> end_frame
//
// When the render graph lands (m5), on_frame's FrameContext hands over a
// graph builder instead of a raw command list; the hook shape stays.

#pragma once
#ifndef ENGINE_APPLICATION_H
#define ENGINE_APPLICATION_H

#include <engine/rhi/rhi.h>

#include <memory>

#include "window/window.h"

namespace engine::app {
    struct ApplicationDesc {
        const char *title = "engine";
        uint32_t width = 1280;
        uint32_t height = 720;
        engine::rhi::Backend backend =
#if defined(_WIN32)
                rhi::Backend::D3D12;
#elif defined(__APPLE__)
                engine::rhi::Backend::Metal;
#elif defined(__linux__)
			    engine::rhi::Backend::Vulkan;
#else
                rhi::Backend::Vulkan;
#endif
        bool enable_debug = true;
    };

    class Application {
    public:
        explicit Application(const ApplicationDesc &);

        virtual ~Application();

        Application(const Application &) = delete;

        Application &operator=(const Application &) = delete;

        // The loop. Returns the process exit code.
        int run();

    protected:
        // ---- hooks, in call order ----
        virtual void on_start() {
        } // device is live
        virtual void on_frame(const rhi::FrameContext &, float dt) = 0;

        virtual void on_resize(uint32_t /*w*/, uint32_t /*h*/) {
        } // after device resize
        virtual void on_stop() {
        } // device still live

        [[nodiscard]] rhi::IDevice &device() { return *device_; }
        [[nodiscard]] Window &window() { return *window_; }
        void request_quit() { quit_ = true; }

    private:
        std::unique_ptr<Window> window_;
        rhi::IDevice *device_ = nullptr;
        bool quit_ = false;
    };
} // namespace eng::app
#endif //ENGINE_APPLICATION_H
