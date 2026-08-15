// eng/app/src/win32/app_win32.cpp
#include <engine/app/window/window.h>

#include <engine/core/asserts.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace engine::app {
    struct WndProcThunk {
        static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            // Window* stored at creation via CREATESTRUCT — the standard dance.
            if (msg == WM_NCCREATE) {
                auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            auto *w = reinterpret_cast<Window *>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!w) return DefWindowProcW(hwnd, msg, wp, lp);

            switch (msg) {
                case WM_SIZE:
                    if (wp != SIZE_MINIMIZED) {
                        w->width_ = LOWORD(lp);
                        w->height_ = HIWORD(lp);
                        w->resized_ = true;
                    }
                    return 0;
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    };

    Window::Window(const WindowDesc &desc) : width_(desc.width), height_(desc.height) {
        const HINSTANCE inst = GetModuleHandleW(nullptr);

        const WNDCLASSW wc{
            .lpfnWndProc = &WndProcThunk::proc,
            .hInstance = inst,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .lpszClassName = L"eng_window",
        };
        RegisterClassW(&wc); // idempotent enough for one window; fine for now

        // Client area (the render target) should be WxH, not the outer frame:
        RECT r{0, 0, LONG(desc.width), LONG(desc.height)};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

        wchar_t wtitle[256];
        MultiByteToWideChar(CP_UTF8, 0, desc.title, -1, wtitle, 256);

        HWND hwnd = CreateWindowExW(
            0, wc.lpszClassName, wtitle, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            r.right - r.left, r.bottom - r.top,
            nullptr, nullptr, inst, this);
        check(hwnd);
        native_ = hwnd;

        ShowWindow(hwnd, SW_SHOW);
    }

    Window::~Window() {
        if (native_) DestroyWindow(static_cast<HWND>(native_));
    }

    bool Window::pump() {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return true;
    }

    bool Window::consume_resize() {
        const bool r = resized_;
        resized_ = false;
        return r;
    }
} // namespace eng::app
