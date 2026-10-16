// The Win32 window of raw/platform/window.hpp (N2). Per-monitor DPI aware, so client sizes are
// physical pixels and equal the swapchain size. WM_SIZE becomes Resized, Minimised or Restored;
// WM_CLOSE becomes CloseRequested and the window stays until the host destroys it; F11 and
// Alt+Enter become ToggleFullscreen (DXGI's own Alt+Enter is disabled by the swapchain).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "raw/platform/window.hpp"
namespace raw::platform {
namespace {
constexpr const wchar_t* kClass = L"raw-native-present";
std::wstring wide(const std::string& s){
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? size_t(n) : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

class Win32Window final : public Window {
public:
    ~Win32Window() override { if (hwnd_) DestroyWindow(hwnd_); }
    bool create(const WindowDesc& d, std::string& err);
    void* nativeHandle() const override { return hwnd_; }
    NativeWindow native() const override { return {NativeWindow::System::Win32, nullptr, hwnd_}; }
    const char* systemName() const override { return "win32"; }
    std::vector<WindowEvent> pump() override {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); }
        std::vector<WindowEvent> out; out.swap(events_);
        return out;
    }
    uint32_t clientWidth() const override { return w_; }
    uint32_t clientHeight() const override { return h_; }
    bool minimised() const override { return minimised_; }
    bool closed() const override { return closed_; }
    void setClientSize(uint32_t w, uint32_t h) override {
        if (fullscreen_) return;
        RECT r{0, 0, (LONG)w, (LONG)h};
        AdjustWindowRectExForDpi(&r, (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE), FALSE, 0, GetDpiForWindow(hwnd_));
        SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void minimise() override { ShowWindow(hwnd_, SW_MINIMIZE); }
    void restore() override { ShowWindow(hwnd_, SW_RESTORE); }
    void setFullscreen(bool on) override;
    bool fullscreen() const override { return fullscreen_; }
    Rect monitorRect() const override {
        MONITORINFO mi{}; mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        return {mi.rcMonitor.left, mi.rcMonitor.top, uint32_t(mi.rcMonitor.right - mi.rcMonitor.left), uint32_t(mi.rcMonitor.bottom - mi.rcMonitor.top)};
    }
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    HWND hwnd_{};
    std::vector<WindowEvent> events_;
    uint32_t w_{0}, h_{0};
    bool minimised_{false}, closed_{false}, fullscreen_{false};
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
};

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    if (msg == WM_NCCREATE){
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Win32Window::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg){
        case WM_SIZE: {
            const uint32_t w = LOWORD(lp), h = HIWORD(lp);
            if (wp == SIZE_MINIMIZED){
                if (!minimised_) events_.push_back({WindowEvent::Kind::Minimised, 0, 0});
                minimised_ = true;
                return 0;
            }
            const bool was = minimised_;
            minimised_ = false;
            if (was) events_.push_back({WindowEvent::Kind::Restored, w, h});
            else if (w != w_ || h != h_) events_.push_back({WindowEvent::Kind::Resized, w, h});
            w_ = w; h_ = h;
            return 0;
        }
        case WM_CLOSE:
            if (!closed_) events_.push_back({WindowEvent::Kind::CloseRequested, w_, h_});
            closed_ = true;
            return 0;   // the host destroys the window when it is done with the swapchain
        case WM_KEYDOWN:
            if (wp == VK_F11){ events_.push_back({WindowEvent::Kind::ToggleFullscreen, w_, h_}); return 0; }
            break;
        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN && (lp & (1 << 29))){ events_.push_back({WindowEvent::Kind::ToggleFullscreen, w_, h_}); return 0; }
            break;
        case WM_SYSCHAR:
            if (wp == VK_RETURN) return 0;   // no beep for Alt+Enter
            break;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Win32Window::setFullscreen(bool on){
    if (on == fullscreen_) return;
    if (on){
        GetWindowPlacement(hwnd_, &placement_);
        const Rect m = monitorRect();
        fullscreen_ = true;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, m.x, m.y, (int)m.width, (int)m.height, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        fullscreen_ = false;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(hwnd_, &placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

bool Win32Window::create(const WindowDesc& d, std::string& err){
    static const bool dpi = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
    (void)dpi;   // false when the process already chose an awareness; sizes are then as Windows reports them
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    if (!GetClassInfoExW(inst, kClass, &wc)){
        wc = {}; wc.cbSize = sizeof wc; wc.lpfnWndProc = wndProc; wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc)){ err = "RegisterClassEx failed: error " + std::to_string(GetLastError()); return false; }
    }
    hwnd_ = CreateWindowExW(0, kClass, wide(d.title).c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            (int)d.width, (int)d.height, nullptr, nullptr, inst, this);
    if (!hwnd_){ err = "CreateWindowEx failed: error " + std::to_string(GetLastError()); return false; }
    setClientSize(d.width, d.height);
    if (d.visible){ ShowWindow(hwnd_, SW_SHOWNOACTIVATE); UpdateWindow(hwnd_); }
    RECT r{}; GetClientRect(hwnd_, &r);
    w_ = uint32_t(r.right - r.left); h_ = uint32_t(r.bottom - r.top);
    pump();   // creation's own WM_SIZE events are not news to the host
    if (w_ == 0 || h_ == 0){ err = "the window has an empty client area"; return false; }
    return true;
}
}  // namespace

std::unique_ptr<Window> createWindow(const WindowDesc& desc, std::string& err){
    auto w = std::make_unique<Win32Window>();
    if (!w->create(desc, err)) return nullptr;
    return w;
}
}  // namespace raw::platform
