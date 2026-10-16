#pragma once
// A native window for presentation (N2). One implementation per platform under
// src/platform/<os>/: Win32 (src/platform/win32/), and on Linux X11 through xcb and Wayland through
// xdg-shell (src/platform/linux/), chosen by RAW_NATIVE_WINDOW=x11|wayland or by WAYLAND_DISPLAY.
// The window reports what happened to it as events; the host decides what to do (resize the
// swapchain, stop presenting while minimised, leave on close). No game input: that is G1 at M3.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace raw::platform {

struct WindowEvent {
    enum class Kind : uint8_t { Resized, Minimised, Restored, CloseRequested, ToggleFullscreen };
    Kind kind{Kind::Resized};
    uint32_t width{0}, height{0};   // the client size after the event (Resized, Restored)
};
struct WindowDesc {
    std::string title{"raw-native"};
    uint32_t width{1280}, height{720};   // client area, in physical pixels
    bool visible{true};
};
struct Rect { int32_t x{0}, y{0}; uint32_t width{0}, height{0}; };
// What a graphics API needs to make a surface: the system, its connection and the window.
struct NativeWindow {
    enum class System : uint8_t { Win32, Xcb, Wayland };
    System system{System::Win32};
    void* display{nullptr};   // xcb_connection_t* or wl_display*; null on Win32
    void* window{nullptr};    // HWND, the xcb_window_t (as an integer) or wl_surface*
};

class Window {
public:
    virtual ~Window() = default;
    virtual void* nativeHandle() const = 0;              // an HWND on Windows
    virtual NativeWindow native() const = 0;
    // False where the window system never tells a client it was minimised (Wayland's xdg-shell).
    virtual bool reportsMinimise() const { return true; }
    virtual const char* systemName() const = 0;         // "win32", "x11" or "wayland"
    // Handle every waiting OS message and return the events they produced, in order.
    virtual std::vector<WindowEvent> pump() = 0;
    virtual uint32_t clientWidth() const = 0;
    virtual uint32_t clientHeight() const = 0;
    virtual bool minimised() const = 0;
    virtual bool closed() const = 0;
    // Programmatic changes, used by hosts and tests; each one produces the matching events.
    virtual void setClientSize(uint32_t width, uint32_t height) = 0;
    virtual void minimise() = 0;
    virtual void restore() = 0;
    // Borderless fullscreen on the monitor that holds the window, and back to the earlier
    // placement. Exclusive fullscreen is not used: flip-model swapchains get independent flip
    // from the compositor in a borderless window that covers its monitor.
    virtual void setFullscreen(bool on) = 0;
    virtual bool fullscreen() const = 0;
    virtual Rect monitorRect() const = 0;
    // The refresh rate of the monitor holding the window in its current mode, in hertz; 0 when the
    // window system does not say (X11 without RandR). Win32: the display configuration's exact
    // rational; Wayland: the output's current mode.
    virtual double refreshHz() const { return 0; }
};
// Null with the reason when the platform has no window system or the window cannot be made.
std::unique_ptr<Window> createWindow(const WindowDesc& desc, std::string& err);

}  // namespace raw::platform
