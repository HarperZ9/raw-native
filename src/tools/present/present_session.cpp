// The host's session (a device it owns, the passes, a swapchain, the targets) and the offscreen
// run that presented runs are compared with.
#include "present_host.hpp"
#include <fstream>
namespace raw::present {
bool writePpm(const std::string& path, const FrameOut& f, uint32_t w, uint32_t h){
    std::ofstream o(path, std::ios::binary);
    o << "P6\n" << w << " " << h << "\n255\n";
    for (size_t i = 0; i + 3 < f.pixels.size(); i += 4) o.write((const char*)&f.pixels[i], 3);
    return bool(o);
}


bool openSession(Session& s, platform::Window* w, rhi::TextureFormat format, rhi::ColourSpace cs, uint32_t sync,
                 bool waitable, std::string& err, bool* noDevice){
    closeSession(s);
    err.clear();   // D3d12Device::init reports success as an empty error string
    s.dev = rhi::createDevice(err);
    if (!s.dev){ if (noDevice) *noDevice = true; return false; }
    const rhi::AdapterInfo& a = s.dev->adapter();
    s.adapter = a.description + " (" + a.backend + ", vendor " + a.vendor + ", driver " + a.driver + ")";
    uint32_t width = 0, height = 0;
    if (w){
        const platform::NativeWindow n = w->native();
        rhi::SwapchainDesc d;
        d.system = n.system == platform::NativeWindow::System::Xcb ? rhi::WindowSystem::Xcb
                 : n.system == platform::NativeWindow::System::Wayland ? rhi::WindowSystem::Wayland : rhi::WindowSystem::Win32;
        d.display = n.display; d.window = n.window; d.width = w->clientWidth(); d.height = w->clientHeight();
        d.format = format; d.colourSpace = cs; d.allowTearing = sync == 0; d.waitable = waitable;
        s.sc = s.dev->createSwapchain(d, err);
        if (!s.sc) return false;
        width = s.sc->width(); height = s.sc->height();
    }
    // The blit draws into the backbuffer, so its pipeline takes the swapchain's format.
    const rhi::TextureFormat blitFormat = s.sc && s.sc->format() == rhi::TextureFormat::BGRA8Unorm ? rhi::TextureFormat::BGRA8Unorm
                                                                                                     : rhi::TextureFormat::RGBA8Unorm;
    if (!createPipelines(*s.dev, blitFormat, s.pipes, err)) return false;
    return !w || makeTargets(*s.dev, width, height, s.targets, err);
}
void closeSession(Session& s){
    if (s.dev) dropTargets(*s.dev, s.targets);
    s.sc.reset();          // the swapchain holds the queue: release it before the device
    s.pipes = {};
    s.dev.reset();
}
bool resizeSession(Session& s, uint32_t w, uint32_t h, std::string& err){
    if (w == 0 || h == 0){ err = "present: refused a resize to an empty size"; return false; }
    dropTargets(*s.dev, s.targets);
    if (s.sc && (s.sc->width() != w || s.sc->height() != h) && !s.sc->resize(w, h, err)) return false;
    return makeTargets(*s.dev, w, h, s.targets, err);
}

int runOffscreen(const RunOptions& o, RunResult& r){
    Session s;
    bool noDevice = false;
    if (!openSession(s, nullptr, rhi::TextureFormat::RGBA8Unorm, rhi::ColourSpace::SrgbNonlinear, o.syncInterval, false, r.error, &noDevice))
        return noDevice ? kSkip : kRuntime;
    r.adapter = s.adapter; r.backend = s.dev->backendName(); r.windowSystem = "none";
    for (uint32_t f = 0; f < o.frames; ++f){
        uint32_t w, h;
        o.schedule.sizeAt(f, o.width, o.height, w, h);
        if ((w != s.targets.width || h != s.targets.height) && !resizeSession(s, w, h, r.error)) return kRuntime;
        FrameSpec spec; spec.frame = f; spec.blitOffsetX = o.control ? 1 : 0; spec.keepPixels = f == 0 && !o.dump.empty();
        FrameOut out;
        if (!runFrame(*s.dev, s.pipes, s.targets, nullptr, spec, out, r.error)){
            r.error = "frame " + std::to_string(f) + ": " + r.error; return kRuntime; }
        if (f == 0) r.graph = out.graph;
        if (spec.keepPixels && !writePpm(o.dump, out, w, h)){ r.error = "cannot write " + o.dump; return kRuntime; }
        r.frames.push_back({f, w, h, out.hash});
    }
    closeSession(s);
    return kOk;
}
}  // namespace raw::present
