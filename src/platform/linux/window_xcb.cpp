// The X11 window (xcb) of raw/platform/window.hpp (N3). ConfigureNotify becomes Resized,
// UnmapNotify Minimised and MapNotify Restored; WM_DELETE_WINDOW becomes CloseRequested.
// Programmatic changes wait for the server's notification (up to two seconds), so a resize is
// seen by the next pump as it is on Win32. Without a window manager (Xvfb in CI) minimise is an
// unmap, which is also what a window manager does when it iconifies a window, and fullscreen is a
// configure to the screen size together with the _NET_WM_STATE_FULLSCREEN request a manager acts on.
#include "window_linux.hpp"
#include <xcb/xcb.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
namespace raw::platform {
namespace {
xcb_atom_t atom(xcb_connection_t* c, const char* name){
    xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)std::strlen(name), name), nullptr);
    const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    std::free(r);
    return a;
}

class XcbWindow final : public Window {
public:
    ~XcbWindow() override {
        if (c_){ if (win_) xcb_destroy_window(c_, win_); xcb_flush(c_); xcb_disconnect(c_); }
    }
    bool create(const WindowDesc& d, std::string& err);
    void* nativeHandle() const override { return (void*)(uintptr_t)win_; }
    NativeWindow native() const override { return {NativeWindow::System::Xcb, c_, (void*)(uintptr_t)win_}; }
    const char* systemName() const override { return "x11"; }
    std::vector<WindowEvent> pump() override { poll(); std::vector<WindowEvent> out; out.swap(events_); return out; }
    uint32_t clientWidth() const override { return w_; }
    uint32_t clientHeight() const override { return h_; }
    bool minimised() const override { return minimised_; }
    bool closed() const override { return closed_; }
    void setClientSize(uint32_t w, uint32_t h) override {
        if (fullscreen_) return;
        configure(0, 0, w, h, false);
        waitFor([&]{ return w_ == w && h_ == h; });
    }
    void minimise() override { xcb_unmap_window(c_, win_); xcb_flush(c_); waitFor([&]{ return minimised_; }); }
    void restore() override { xcb_map_window(c_, win_); xcb_flush(c_); waitFor([&]{ return !minimised_; }); }
    void setFullscreen(bool on) override;
    bool fullscreen() const override { return fullscreen_; }
    Rect monitorRect() const override { return {0, 0, screen_->width_in_pixels, screen_->height_in_pixels}; }

private:
    void configure(int32_t x, int32_t y, uint32_t w, uint32_t h, bool move){
        const uint32_t v[4] = {(uint32_t)x, (uint32_t)y, w, h};
        if (move) xcb_configure_window(c_, win_, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, v);
        else xcb_configure_window(c_, win_, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, v + 2);
        xcb_flush(c_);
    }
    void poll(){ while (xcb_generic_event_t* e = xcb_poll_for_event(c_)){ handle(e); std::free(e); } }
    template<class F> bool waitFor(F done){
        for (int i = 0; i < 2000; ++i){ poll(); if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        return done();
    }
    void handle(xcb_generic_event_t* e);
    void netWmFullscreen(bool on);
    xcb_connection_t* c_{nullptr};
    xcb_screen_t* screen_{nullptr};
    xcb_window_t win_{0};
    xcb_atom_t protocols_{0}, deleteWindow_{0}, wmState_{0}, wmFullscreen_{0};
    std::vector<WindowEvent> events_;
    uint32_t w_{0}, h_{0}, savedW_{0}, savedH_{0};
    bool minimised_{false}, closed_{false}, fullscreen_{false}, mapped_{false};
};

void XcbWindow::handle(xcb_generic_event_t* e){
    switch (e->response_type & ~0x80){
        case XCB_CONFIGURE_NOTIFY: {
            auto* c = (xcb_configure_notify_event_t*)e;
            if (c->window != win_ || (c->width == w_ && c->height == h_)) break;
            w_ = c->width; h_ = c->height;
            if (!minimised_) events_.push_back({WindowEvent::Kind::Resized, w_, h_});
            break;
        }
        case XCB_UNMAP_NOTIFY:
            if (((xcb_unmap_notify_event_t*)e)->window == win_ && !minimised_){ minimised_ = true; events_.push_back({WindowEvent::Kind::Minimised, 0, 0}); }
            break;
        case XCB_MAP_NOTIFY:
            if (((xcb_map_notify_event_t*)e)->window != win_) break;
            if (minimised_){ minimised_ = false; events_.push_back({WindowEvent::Kind::Restored, w_, h_}); }
            mapped_ = true;
            break;
        case XCB_CLIENT_MESSAGE: {
            auto* m = (xcb_client_message_event_t*)e;
            if (m->type == protocols_ && m->data.data32[0] == deleteWindow_ && !closed_){
                closed_ = true; events_.push_back({WindowEvent::Kind::CloseRequested, w_, h_}); }
            break;
        }
        default: break;
    }
}

void XcbWindow::netWmFullscreen(bool on){
    xcb_client_message_event_t m{};
    m.response_type = XCB_CLIENT_MESSAGE; m.format = 32; m.window = win_; m.type = wmState_;
    m.data.data32[0] = on ? 1 : 0; m.data.data32[1] = wmFullscreen_; m.data.data32[3] = 1;
    xcb_send_event(c_, 0, screen_->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, (const char*)&m);
}
void XcbWindow::setFullscreen(bool on){
    if (on == fullscreen_) return;
    if (on){ savedW_ = w_; savedH_ = h_; }
    fullscreen_ = on;
    netWmFullscreen(on);
    const uint32_t w = on ? screen_->width_in_pixels : savedW_, h = on ? screen_->height_in_pixels : savedH_;
    configure(0, 0, w, h, true);
    waitFor([&]{ return w_ == w && h_ == h; });
}

bool XcbWindow::create(const WindowDesc& d, std::string& err){
    int screenIndex = 0;
    c_ = xcb_connect(nullptr, &screenIndex);
    if (!c_ || xcb_connection_has_error(c_)){ err = "cannot connect to the X server (DISPLAY not set or no server)"; return false; }
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(c_));
    for (int i = 0; i < screenIndex && it.rem; ++i) xcb_screen_next(&it);
    screen_ = it.data;
    if (!screen_){ err = "the X server has no screen"; return false; }
    win_ = xcb_generate_id(c_);
    const uint32_t values[2] = {screen_->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(c_, XCB_COPY_FROM_PARENT, win_, screen_->root, 0, 0, (uint16_t)d.width, (uint16_t)d.height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen_->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    xcb_change_property(c_, XCB_PROP_MODE_REPLACE, win_, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, (uint32_t)d.title.size(), d.title.data());
    protocols_ = atom(c_, "WM_PROTOCOLS"); deleteWindow_ = atom(c_, "WM_DELETE_WINDOW");
    wmState_ = atom(c_, "_NET_WM_STATE"); wmFullscreen_ = atom(c_, "_NET_WM_STATE_FULLSCREEN");
    xcb_change_property(c_, XCB_PROP_MODE_REPLACE, win_, protocols_, XCB_ATOM_ATOM, 32, 1, &deleteWindow_);
    w_ = d.width; h_ = d.height;
    if (d.visible){
        xcb_map_window(c_, win_);
        xcb_flush(c_);
        if (!waitFor([&]{ return mapped_; })){ err = "the X server did not map the window"; return false; }
    }
    xcb_flush(c_);
    events_.clear();   // creation's own events are not news to the host
    return true;
}
}  // namespace

std::unique_ptr<Window> createXcbWindow(const WindowDesc& desc, std::string& err){
    auto w = std::make_unique<XcbWindow>();
    if (!w->create(desc, err)) return nullptr;
    return w;
}
}  // namespace raw::platform
