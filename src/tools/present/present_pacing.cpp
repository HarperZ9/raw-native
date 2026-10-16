// Frame pacing (N2 criterion 4, evidence/n2-presentation-bounds.json c4_pacing_rtx4090): the
// present-to-present interval from QueryPerformanceCounter at the return of each Present, and
// DXGI's frame statistics where the output gives them. The record is judged by
// tests/gpu/present_gate.py pacing against the bound written before the RTX run.
#include "present_host.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
namespace raw::present {
namespace {
double percentile(std::vector<double> v, double q){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double at = q * double(v.size() - 1);
    const size_t lo = (size_t)at;
    const double t = at - double(lo);
    return lo + 1 < v.size() ? v[lo] * (1 - t) + v[lo + 1] * t : v[lo];
}
}  // namespace

int pacingCommand(int argc, char** argv){
    uint32_t frames = 10000, warmup = 120, sync = 1, width = 1280, height = 720;
    bool fullscreen = false;
    std::string out;
    for (int i = 1; i < argc; ++i){
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out") out = next();
        else if (a == "--frames") frames = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--warmup") warmup = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--width") width = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--height") height = (uint32_t)std::strtoul(next(), nullptr, 10);
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--vsync") sync = std::strcmp(next(), "off") == 0 ? 0 : 1;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return kUsage; }
    }
    if (out.empty() || frames < 2){ std::fprintf(stderr, "pacing: --out is required and --frames at least 2\n"); return kUsage; }
    std::string err;
    platform::WindowDesc wd; wd.title = "raw-native present pacing"; wd.width = width; wd.height = height;
    auto win = platform::createWindow(wd, err);
    if (!win){ std::printf("SKIP: %s\n", err.c_str()); return kSkip; }
    Session s; bool noDevice = false;
    if (!openSession(s, win.get(), rhi::TextureFormat::RGBA8Unorm, rhi::ColourSpace::SrgbNonlinear, sync, true, err, &noDevice)){
        if (noDevice){ std::printf("SKIP: %s\n", err.c_str()); return kSkip; }
        std::fprintf(stderr, "pacing: %s\n", err.c_str()); return kRuntime;
    }
    if (fullscreen){
        win->setFullscreen(true);
        for (int k = 0; k < 200; ++k){
            for (const auto& e : win->pump())
                if (e.width && e.height && !resizeSession(s, e.width, e.height, err)){ std::fprintf(stderr, "pacing: %s\n", err.c_str()); return kRuntime; }
            if (s.sc->width() == win->clientWidth() && s.sc->height() == win->clientHeight() && win->fullscreen()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    const double hz = s.sc->refreshHz();
    std::vector<int64_t> t; t.reserve(frames);
    std::vector<rhi::PresentStats> st; st.reserve(frames);
    uint32_t occluded = 0, timeouts = 0;
    for (uint32_t i = 0; i < warmup + frames; ++i){
        for (const auto& e : win->pump()) (void)e;   // a moved or covered window is recorded by DXGI, not handled here
        if (!s.sc->waitForFrame(1000)) ++timeouts;
        FrameSpec spec; spec.frame = i; spec.syncInterval = sync; spec.readback = false;
        FrameOut fo;
        if (!runFrame(*s.dev, s.pipes, s.targets, s.sc.get(), spec, fo, err)){ std::fprintf(stderr, "pacing frame %u: %s\n", i, err.c_str()); return kRuntime; }
        const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
        occluded += fo.status == rhi::PresentStatus::Occluded;
        if (i >= warmup){ t.push_back(now); st.push_back(s.sc->stats()); }
    }
    const double tick = double(std::chrono::steady_clock::period::num) / double(std::chrono::steady_clock::period::den) * 1e3;
    std::vector<double> ms;
    for (size_t i = 1; i < t.size(); ++i) ms.push_back(double(t[i] - t[i - 1]) * tick);
    uint64_t statsValid = 0, missed = 0;
    for (size_t i = 1; i < st.size(); ++i){
        if (!st[i].valid || !st[i - 1].valid || st[i].presentCount == st[i - 1].presentCount) continue;
        ++statsValid;
        const uint32_t d = st[i].presentRefreshCount - st[i - 1].presentRefreshCount;
        if (d > 1) missed += d - 1;
    }
    double mean = 0; for (double v : ms) mean += v; mean /= double(ms.size());
    const double period = hz > 0 ? 1e3 / hz : 0;
    uint64_t above = 0; for (double v : ms) above += period > 0 && v > 1.5 * period;
    std::string j = "{\n \"schema\": \"raw-native.present-pacing/1\",\n \"adapter\": " + jstr(s.adapter) + ",\n \"refresh_hz\": " + jnum(hz)
        + ",\n \"period_ms\": " + jnum(period)
        + ",\n \"clock\": \"std::chrono::steady_clock (QueryPerformanceCounter on MSVC), read when Present returns\""
        + ",\n \"frames\": " + std::to_string(frames) + ", \"warmup\": " + std::to_string(warmup)
        + ",\n \"sync_interval\": " + std::to_string(sync) + ", \"tearing\": " + (s.sc->tearingAllowed() ? "true" : "false")
        + ", \"fullscreen\": " + (win->fullscreen() ? "true" : "false") + ", \"size\": " + jstr(std::to_string(s.sc->width()) + "x" + std::to_string(s.sc->height()))
        + ",\n \"occluded_presents\": " + std::to_string(occluded) + ", \"wait_timeouts\": " + std::to_string(timeouts)
        + ",\n \"summary_ms\": {\"mean\": " + jnum(mean) + ", \"median\": " + jnum(percentile(ms, 0.5)) + ", \"p95\": " + jnum(percentile(ms, 0.95))
        + ", \"p99\": " + jnum(percentile(ms, 0.99)) + ", \"max\": " + jnum(percentile(ms, 1.0)) + ", \"min\": " + jnum(percentile(ms, 0.0))
        + ", \"above_1_5_period\": " + std::to_string(above) + "},\n \"dxgi\": {\"intervals_with_stats\": " + std::to_string(statsValid)
        + ", \"missed_refreshes\": " + std::to_string(missed) + "},\n \"intervals_ms\": [";
    for (size_t i = 0; i < ms.size(); ++i) j += (i ? (i % 16 ? ", " : ",\n  ") : "\n  ") + jnum(ms[i]);
    j += "\n ]\n}\n";
    closeSession(s);
    if (!writeText(out, j)){ std::fprintf(stderr, "cannot write %s\n", out.c_str()); return kRuntime; }
    std::printf("pacing: %zu intervals, %.3f Hz, median %.4f ms, p99 %.4f ms, above 1.5T %llu, missed refreshes %llu\n", ms.size(), hz,
                percentile(ms, 0.5), percentile(ms, 0.99), (unsigned long long)above, (unsigned long long)missed);
    return kOk;
}
}  // namespace raw::present
