// The presented run: a window, a swapchain, the frame loop with its lifecycle events (resize,
// minimise, device removal, fullscreen), and the command line of raw_native_present.
#include "present_host.hpp"
#include "raw/tools/present_cmd.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
namespace raw::present {
namespace {
std::string size(uint32_t w, uint32_t h){ return std::to_string(w) + "x" + std::to_string(h); }
void nap(int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Apply the window's events to the session until the window has none left.
bool handleEvents(platform::Window& win, Session& s, RunResult& r, bool& closeRequested){
    using K = platform::WindowEvent::Kind;
    for (auto ev = win.pump(); !ev.empty(); ev = win.pump()){
        for (const platform::WindowEvent& e : ev){
            if (e.kind == K::CloseRequested){ closeRequested = true; r.events.push_back("close requested"); continue; }
            if (e.kind == K::Minimised){ r.events.push_back("minimised"); continue; }
            if (e.kind == K::ToggleFullscreen){ win.setFullscreen(!win.fullscreen()); continue; }
            r.events.push_back(std::string(e.kind == K::Restored ? "restored " : "resized ") + size(e.width, e.height));
            if (e.width == 0 || e.height == 0){ ++r.zeroResizes; continue; }   // never resized to nothing
            if (!resizeSession(s, e.width, e.height, r.error)) return false;
        }
    }
    return true;
}
// Pump until `done` holds or two seconds pass.
template<class F> bool settle(platform::Window& win, Session& s, RunResult& r, bool& close, F done){
    for (int i = 0; i < 200; ++i){
        if (!handleEvents(win, s, r, close)) return false;
        if (done()) return true;
        nap(10);
    }
    return done();
}
bool recover(platform::Window& win, Session& s, const RunOptions& o, RunResult& r, uint32_t f, const std::string& why){
    r.events.push_back("device lost at frame " + std::to_string(f) + ": " + why);
    if (++r.recoveries > 3){ r.error = "more than three device losses"; return false; }
    std::string err;
    if (!openSession(s, &win, rhi::TextureFormat::RGBA8Unorm, rhi::ColourSpace::SrgbNonlinear, o.syncInterval, false, err)){
        r.error = "recovery: " + err; return false; }
    r.events.push_back("recovered on a new device and swapchain " + size(s.sc->width(), s.sc->height()));
    return true;
}
// Fullscreen on the window's monitor and back; frames drawn there must equal offscreen frames.
bool fullscreenCheck(platform::Window& win, Session& s, const RunOptions& o, RunResult& r, bool& close){
    r.fullscreenChecked = true;
    const platform::Rect m = win.monitorRect();
    const uint32_t pw = s.sc->width(), ph = s.sc->height();
    win.setFullscreen(true);
    const bool in = settle(win, s, r, close, [&]{ return s.sc->width() == m.width && s.sc->height() == m.height; });
    bool same = in;
    for (uint32_t k = 0; same && k < 3; ++k){
        FrameSpec spec; spec.frame = o.frames + k; spec.syncInterval = o.syncInterval;
        FrameOut a, b;
        same = runFrame(*s.dev, s.pipes, s.targets, s.sc.get(), spec, a, r.error)
            && runFrame(*s.dev, s.pipes, s.targets, nullptr, spec, b, r.error) && a.hash == b.hash;
    }
    win.setFullscreen(false);
    const bool out = settle(win, s, r, close, [&]{ return s.sc->width() == pw && s.sc->height() == ph; });
    r.events.push_back("fullscreen " + size(m.width, m.height) + (in ? " reached" : " not reached") + (same ? ", frames equal offscreen" : ", frames differ")
                       + (out ? ", left to " : ", did not return to ") + size(pw, ph));
    r.fullscreenOk = in && same && out;
    return true;
}
}  // namespace

int runPresented(const RunOptions& o, RunResult& r){
    platform::WindowDesc wd; wd.title = "raw-native present"; wd.width = o.width; wd.height = o.height; wd.visible = !o.hidden;
    auto win = platform::createWindow(wd, r.error);
    if (!win) return kSkip;
    Session s;
    bool noDevice = false, close = false;
    if (!openSession(s, win.get(), rhi::TextureFormat::RGBA8Unorm, rhi::ColourSpace::SrgbNonlinear, o.syncInterval, false, r.error, &noDevice))
        return noDevice ? kSkip : kRuntime;
    r.adapter = s.adapter; r.tearing = s.sc->tearingAllowed();
    if (s.sc->width() != o.width || s.sc->height() != o.height)
        r.events.push_back("the window's client area is " + size(s.sc->width(), s.sc->height()) + ", not " + size(o.width, o.height));
    const Schedule& sch = o.schedule;
    uint32_t skipped = 0;
    bool minimiseNoted = false;
    r.windowSystem = win->systemName(); r.backend = s.dev->backendName();
    for (uint32_t f = 0; f < o.frames && !close;){
        if (sch.on && skipped == 0){
            if (f == sch.resizeA || f == sch.resizeB){ uint32_t w, h; sch.sizeAt(f, o.width, o.height, w, h); win->setClientSize(w, h); }
            if (f == sch.minimise && !win->reportsMinimise() && !minimiseNoted){
                minimiseNoted = true;
                r.events.push_back(std::string("minimise skipped: ") + win->systemName() + " does not tell a client it was minimised");
            } else if (f == sch.minimise && !win->minimised() && win->reportsMinimise()){
                win->minimise();
                if (!settle(*win, s, r, close, [&]{ return win->minimised(); })) return kRuntime;
            }
            if (f == sch.lose && r.recoveries == 0){
                std::string e;
                const bool injected = std::strcmp(s.dev->backendName(), "vulkan") == 0;   // Vulkan has no removal call
                r.events.push_back(s.dev->loseForTest(e) ? "device removed before frame " + std::to_string(f) + (injected ? " (injected at the RHI)" : "")
                                                         : "removal refused: " + e);
            }
        }
        if (!handleEvents(*win, s, r, close)) return kRuntime;
        if (win->minimised()){   // nothing is drawn or presented while the client area is empty
            if (++skipped >= 10 && sch.on) win->restore();
            nap(5);
            continue;
        }
        skipped = 0;
        uint32_t w = s.sc->width(), h = s.sc->height();
        if (sch.on){
            sch.sizeAt(f, o.width, o.height, w, h);
            if (w != s.sc->width() || h != s.sc->height()){
                ++r.sizeMismatches;
                r.events.push_back("frame " + std::to_string(f) + ": swapchain " + size(s.sc->width(), s.sc->height()) + ", scheduled " + size(w, h));
            }
        }
        FrameSpec spec; spec.frame = f; spec.blitOffsetX = o.control ? 1 : 0; spec.syncInterval = o.syncInterval; spec.readback = o.readback;
        spec.keepPixels = f == 0 && o.readback && !o.dump.empty();
        FrameOut out;
        if (!runFrame(*s.dev, s.pipes, s.targets, s.sc.get(), spec, out, r.error)){
            if (out.status == rhi::PresentStatus::DeviceLost || s.dev->lost()){
                if (!recover(*win, s, o, r, f, r.error)) return kRuntime;
                r.error.clear();
                continue;   // draw the same frame again on the new device
            }
            r.error = "frame " + std::to_string(f) + ": " + r.error;
            return kRuntime;
        }
        if (f == 0) r.graph = out.graph;
        if (spec.keepPixels && !writePpm(o.dump, out, s.sc->width(), s.sc->height())){ r.error = "cannot write " + o.dump; return kRuntime; }
        r.frames.push_back({f, s.sc->width(), s.sc->height(), out.hash});
        ++f;
    }
    if (o.fullscreenCheck && !close && !fullscreenCheck(*win, s, o, r, close)) return kRuntime;
    closeSession(s);
    const bool lifecycleOk = !sch.on || (r.recoveries == 1 && r.sizeMismatches == 0);
    return lifecycleOk && (!r.fullscreenChecked || r.fullscreenOk) ? kOk : kCheckFailed;
}
}  // namespace raw::present

namespace raw {
int presentCommand(int argc, char** argv){
    using namespace present;
    if (argc < 2){ std::fprintf(stderr, "usage: raw_native_present run|hdr|pacing|selftest ... (raw/tools/present_cmd.hpp)\n"); return kUsage; }
    const std::string cmd = argv[1];
    if (cmd == "selftest") return selftest();
    if (cmd == "pacing") return pacingCommand(argc - 1, argv + 1);
    RunOptions o;
    for (int i = 2; i < argc; ++i){
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out") o.out = next();
        else if (a == "--frames") o.frames = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--width") o.width = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--height") o.height = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--offscreen") o.offscreen = true;
        else if (a == "--lifecycle") o.schedule.on = true;
        else if (a == "--control") o.control = true;
        else if (a == "--vsync") o.syncInterval = std::strcmp(next(), "off") == 0 ? 0 : 1;
        else if (a == "--no-readback") o.readback = false;
        else if (a == "--fullscreen-check") o.fullscreenCheck = true;
        else if (a == "--hidden") o.hidden = true;
        else if (a == "--dump") o.dump = next();
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return kUsage; }
    }
    if (o.out.empty()){ std::fprintf(stderr, "--out is required\n"); return kUsage; }
    if (cmd == "hdr") return hdrCommand(o.out, false);
    if (cmd != "run"){ std::fprintf(stderr, "unknown command %s\n", cmd.c_str()); return kUsage; }
    RunResult r;
    const int rc = o.offscreen ? runOffscreen(o, r) : runPresented(o, r);
    if (rc == kSkip){ std::printf("SKIP: %s\n", r.error.c_str()); return kSkip; }
    if (!writeText(o.out, runJson(o, r))){ std::fprintf(stderr, "cannot write %s\n", o.out.c_str()); return kRuntime; }
    std::printf("%s: %zu frames on %s, recoveries %u, exit %d%s%s\n", o.offscreen ? "offscreen" : "presented", r.frames.size(),
                r.adapter.c_str(), r.recoveries, rc, r.error.empty() ? "" : ", error: ", r.error.c_str());
    return rc;
}
}  // namespace raw
