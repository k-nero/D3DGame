// eng/app/include/eng/app/window.h — minimal platform window.
// Deliberately tiny: create, pump, query. Input mapping arrives in m7.
#pragma once

#include <cstdint>

namespace engine::app {
    struct WindowDesc {
        const char *title = "engine";
        uint32_t width = 1280;
        uint32_t height = 720;
    };

    class Window {
    public:
        explicit Window(const WindowDesc &);

        ~Window();

        Window(const Window &) = delete;

        Window &operator=(const Window &) = delete;

        // Drains the OS message queue. Returns false once the window is closed.
        [[nodiscard]] bool pump();

        [[nodiscard]] void* native_handle() const; // HWND
        [[nodiscard]] uint32_t width() const { return width_; }
        [[nodiscard]] uint32_t height() const { return height_; }

        // True exactly once after a size change settles (not per WM_SIZE spam);
        // caller consumes it and calls device->resize().
        [[nodiscard]] bool consume_resize();

    private:
        void* impl_ = nullptr;
        uint32_t width_ = 0, height_ = 0;
        bool resized_ = false;
        bool should_close_ = false;
        friend struct PlatformWindowBridge;
    };
} // namespace eng::app
