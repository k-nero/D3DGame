// eng/app/src/macos/app_macos.mm — NSWindow + CAMetalLayer behind the
// portable Window interface. Compile with -fobjc-arc (CMake snippet below).
#include <engine/app/window/window.h>
#include <engine/core/asserts.h>

#import <AppKit/AppKit.h>        // #import = #include with built-in include-guard
#import <QuartzCore/CAMetalLayer.h>

// ---------------------------------------------------------------- delegate
// Cocoa's WndProc: an object the window messages on events we care about.
@interface EngWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) engine::app::Window* owner;   // assign = raw ptr,
@end                                                    // C++ object: ARC hands off

// The bridge the header befriended — lets this TU poke Window's privates.
namespace engine::app {
struct PlatformWindowBridge {
    static void set_size(Window& w, uint32_t px_w, uint32_t px_h) {
        w.width_ = px_w; w.height_ = px_h; w.resized_ = true;
    }
    static void request_close(Window& w) { w.should_close_ = true; }
};
} // namespace eng::app
// NOTE: add `bool should_close_ = false;` next to resized_ in window.h.

@implementation EngWindowDelegate
- (void)windowDidResize:(NSNotification*)note {          // ~WM_SIZE
    NSWindow* win = note.object;
    const CGFloat scale = win.backingScaleFactor;        // Retina: points -> pixels
    const NSSize pts = win.contentView.bounds.size;
    engine::app::PlatformWindowBridge::set_size(
        *self.owner,
        (uint32_t)(pts.width * scale),
        (uint32_t)(pts.height * scale));
}
- (void)windowWillClose:(NSNotification*)note {          // ~WM_DESTROY
    engine::app::PlatformWindowBridge::request_close(*self.owner);
}
@end

namespace engine::app {

// ObjC strong pointers inside a C++ struct: legal in ObjC++ with ARC, and
// `delete` releases them — this struct IS our ownership of the Cocoa objects.
struct MacWindowState {
    NSWindow*          window   = nil;
    EngWindowDelegate* delegate = nil;
    CAMetalLayer*      layer    = nil;
};

Window::Window(const WindowDesc& desc) {
    @autoreleasepool {
        // ---- app-wide setup (idempotent; fine for one window) ----
        [NSApplication sharedApplication];
        // Regular = real app: dock icon, can take focus. Without a .app
        // bundle this is what makes a terminal-launched binary show a window.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        auto* state = new MacWindowState();
        impl_ = state;

        const NSRect rect = NSMakeRect(0, 0, desc.width, desc.height); // POINTS
        state->window = [[NSWindow alloc]
            initWithContentRect:rect
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        // THE gotcha: programmatic NSWindows default to self-releasing on
        // close, which double-frees under ARC. Turn it off — we own it.
        state->window.releasedWhenClosed = NO;
        state->window.title = [NSString stringWithUTF8String:desc.title];

        state->delegate = [EngWindowDelegate new];
        state->delegate.owner = this;
        state->window.delegate = state->delegate;

        // ---- the Metal surface: a layer-hosting content view ----
        state->layer = [CAMetalLayer layer];
        state->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;   // = Format::BGRA8_UNorm
        NSView* view = state->window.contentView;
        view.layer = state->layer;      // ORDER MATTERS: layer first,
        view.wantsLayer = YES;          // then wantsLayer => "layer-hosting" view

        const CGFloat scale = state->window.backingScaleFactor;
        state->layer.contentsScale = scale;                    // no Retina blur
        width_  = (uint32_t)(desc.width  * scale);             // report PIXELS,
        height_ = (uint32_t)(desc.height * scale);             // matching win32

        [state->window center];
        [state->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];   // deprecation warning on 14+: harmless
    }
}

Window::~Window() {
    auto* state = static_cast<MacWindowState*>(impl_);
    if (state) {
        state->window.delegate = nil;
        [state->window close];
        delete state;                  // ARC releases window/delegate/layer here
    }
}

bool Window::pump() {
    if (should_close_) return false;
    @autoreleasepool {                 // events are autoreleased — pool per pump
        for (;;) {
            NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantPast]  // = don't block
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) break;
            [NSApp sendEvent:ev];      // routes to window/delegate — this is
        }                              // DispatchMessage. NEVER [NSApp run]:
    }                                  // that steals the loop (axis 2 says no)
    return !should_close_;
}

bool Window::consume_resize() {
    const bool r = resized_;
    resized_ = false;
    return r;
}

void* Window::native_handle() const {
    // The Metal device wants the LAYER (nextDrawable lives on it), not the
    // NSWindow. __bridge: pointer out, ownership stays with MacWindowState.
    return (__bridge void*)static_cast<MacWindowState*>(impl_)->layer;
}

} // namespace eng::app