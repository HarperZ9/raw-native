// The Wayland window (xdg-shell) of raw/platform/window.hpp (N3). A toplevel takes its size from
// what the client draws, so setClientSize records the size and reports Resized, and the swapchain
// built at that size gives the surface its size. A configure from the compositor with a non-zero
// size (fullscreen, a maximise) becomes Resized; close becomes CloseRequested. xdg-shell has no
// event that tells a client it was minimised, so reportsMinimise() is false and minimise() only asks.
#include "window_linux.hpp"
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <cstring>
namespace raw::platform {
namespace {
class WaylandWindow final : public Window {
public:
    ~WaylandWindow() override {
        if (top_) xdg_toplevel_destroy(top_);
        if (xs_) xdg_surface_destroy(xs_);
        if (surf_) wl_surface_destroy(surf_);
        if (out_) wl_output_destroy(out_);
        if (wm_) xdg_wm_base_destroy(wm_);
        if (comp_) wl_compositor_destroy(comp_);
        if (reg_) wl_registry_destroy(reg_);
        if (d_) wl_display_disconnect(d_);
    }
    bool create(const WindowDesc& d, std::string& err);
    void* nativeHandle() const override { return surf_; }
    NativeWindow native() const override { return {NativeWindow::System::Wayland, d_, surf_}; }
    bool reportsMinimise() const override { return false; }
    const char* systemName() const override { return "wayland"; }
    std::vector<WindowEvent> pump() override {
        wl_display_roundtrip(d_);
        std::vector<WindowEvent> out; out.swap(events_);
        return out;
    }
    uint32_t clientWidth() const override { return w_; }
    uint32_t clientHeight() const override { return h_; }
    bool minimised() const override { return false; }
    bool closed() const override { return closed_; }
    void setClientSize(uint32_t w, uint32_t h) override {
        if (fullscreen_ || (w == w_ && h == h_)) return;
        w_ = w; h_ = h;
        events_.push_back({WindowEvent::Kind::Resized, w_, h_});
    }
    void minimise() override { xdg_toplevel_set_minimized(top_); wl_display_flush(d_); }
    void restore() override {}
    void setFullscreen(bool on) override;
    bool fullscreen() const override { return fullscreen_; }
    Rect monitorRect() const override { return {0, 0, outW_, outH_}; }
    double refreshHz() const override { return refreshMhz_ > 0 ? double(refreshMhz_) / 1000.0 : 0; }

    // Listener callbacks.
    static void global(void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version);
    static void globalRemove(void*, wl_registry*, uint32_t){}
    static void ping(void*, xdg_wm_base* wm, uint32_t serial){ xdg_wm_base_pong(wm, serial); }
    static void surfaceConfigure(void* data, xdg_surface* s, uint32_t serial);
    static void toplevelConfigure(void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array* states);
    static void toplevelClose(void* data, xdg_toplevel*){
        auto* self = (WaylandWindow*)data;
        if (!self->closed_){ self->closed_ = true; self->events_.push_back({WindowEvent::Kind::CloseRequested, self->w_, self->h_}); }
    }
    static void outGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t){}
    static void outMode(void* data, wl_output*, uint32_t flags, int32_t w, int32_t h, int32_t refreshMhz){
        auto* self = (WaylandWindow*)data;
        if (flags & WL_OUTPUT_MODE_CURRENT){ self->outW_ = (uint32_t)w; self->outH_ = (uint32_t)h; self->refreshMhz_ = refreshMhz; }
    }
    static void outDone(void*, wl_output*){}
    static void outScale(void*, wl_output*, int32_t){}

private:
    wl_display* d_{nullptr};
    wl_registry* reg_{nullptr};
    wl_compositor* comp_{nullptr};
    xdg_wm_base* wm_{nullptr};
    wl_output* out_{nullptr};
    wl_surface* surf_{nullptr};
    xdg_surface* xs_{nullptr};
    xdg_toplevel* top_{nullptr};
    std::vector<WindowEvent> events_;
    uint32_t w_{0}, h_{0}, savedW_{0}, savedH_{0}, outW_{0}, outH_{0};
    int32_t pendingW_{0}, pendingH_{0}, refreshMhz_{0};
    bool configured_{false}, closed_{false}, fullscreen_{false};
};

const wl_registry_listener kRegistry = {WaylandWindow::global, WaylandWindow::globalRemove};
const xdg_wm_base_listener kWmBase = {WaylandWindow::ping};
const xdg_surface_listener kSurface = {WaylandWindow::surfaceConfigure};
const xdg_toplevel_listener kToplevel = {WaylandWindow::toplevelConfigure, WaylandWindow::toplevelClose};
const wl_output_listener kOutput = {WaylandWindow::outGeometry, WaylandWindow::outMode, WaylandWindow::outDone, WaylandWindow::outScale};

void WaylandWindow::global(void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version){
    auto* self = (WaylandWindow*)data;
    if (std::strcmp(iface, wl_compositor_interface.name) == 0)
        self->comp_ = (wl_compositor*)wl_registry_bind(r, name, &wl_compositor_interface, version < 4 ? version : 4);
    else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0){
        self->wm_ = (xdg_wm_base*)wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(self->wm_, &kWmBase, self);
    } else if (std::strcmp(iface, wl_output_interface.name) == 0 && !self->out_){
        self->out_ = (wl_output*)wl_registry_bind(r, name, &wl_output_interface, version < 2 ? version : 2);
        wl_output_add_listener(self->out_, &kOutput, self);
    }
}
void WaylandWindow::toplevelConfigure(void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array* states){
    auto* self = (WaylandWindow*)data;
    self->pendingW_ = w; self->pendingH_ = h;
    bool full = false;
    for (auto* s = (uint32_t*)states->data; (const char*)s < (const char*)states->data + states->size; ++s)
        full = full || *s == XDG_TOPLEVEL_STATE_FULLSCREEN;
    self->fullscreen_ = full;
}
// A size of 0 leaves it to the client: the size before fullscreen, or the one it has.
void WaylandWindow::surfaceConfigure(void* data, xdg_surface* s, uint32_t serial){
    auto* self = (WaylandWindow*)data;
    xdg_surface_ack_configure(s, serial);
    self->configured_ = true;
    uint32_t w = self->pendingW_ > 0 ? (uint32_t)self->pendingW_ : self->w_, h = self->pendingH_ > 0 ? (uint32_t)self->pendingH_ : self->h_;
    if (!self->fullscreen_ && self->savedW_ && self->pendingW_ == 0){ w = self->savedW_; h = self->savedH_; self->savedW_ = self->savedH_ = 0; }
    if (w && h && (w != self->w_ || h != self->h_)){ self->w_ = w; self->h_ = h; self->events_.push_back({WindowEvent::Kind::Resized, w, h}); }
}

void WaylandWindow::setFullscreen(bool on){
    if (on == fullscreen_) return;
    if (on){ savedW_ = w_; savedH_ = h_; xdg_toplevel_set_fullscreen(top_, nullptr); }
    else xdg_toplevel_unset_fullscreen(top_);
    for (int i = 0; i < 20 && fullscreen_ != on; ++i) wl_display_roundtrip(d_);
}

bool WaylandWindow::create(const WindowDesc& d, std::string& err){
    d_ = wl_display_connect(nullptr);
    if (!d_){ err = "cannot connect to a Wayland compositor (WAYLAND_DISPLAY not set or no compositor)"; return false; }
    reg_ = wl_display_get_registry(d_);
    wl_registry_add_listener(reg_, &kRegistry, this);
    wl_display_roundtrip(d_);
    wl_display_roundtrip(d_);   // the output's mode arrives after it is bound
    if (!comp_ || !wm_){ err = "the Wayland compositor offers no wl_compositor or xdg_wm_base"; return false; }
    surf_ = wl_compositor_create_surface(comp_);
    xs_ = xdg_wm_base_get_xdg_surface(wm_, surf_);
    xdg_surface_add_listener(xs_, &kSurface, this);
    top_ = xdg_surface_get_toplevel(xs_);
    xdg_toplevel_add_listener(top_, &kToplevel, this);
    xdg_toplevel_set_title(top_, d.title.c_str());
    xdg_toplevel_set_app_id(top_, "raw-native");
    w_ = d.width; h_ = d.height;
    wl_surface_commit(surf_);
    for (int i = 0; i < 20 && !configured_; ++i) wl_display_roundtrip(d_);
    if (!configured_){ err = "the Wayland compositor did not configure the window"; return false; }
    events_.clear();   // creation's own configure is not news to the host
    return true;
}
}  // namespace

std::unique_ptr<Window> createWaylandWindow(const WindowDesc& desc, std::string& err){
    auto w = std::make_unique<WaylandWindow>();
    if (!w->create(desc, err)) return nullptr;
    return w;
}
}  // namespace raw::platform
