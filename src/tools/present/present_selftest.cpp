// raw_native_present selftest: a short pass of every N2 check in one process, for CTest. The full
// checks run in CI as separate processes (600 frames, two presented runs, tests/gpu/present_gate.py).
#include "present_host.hpp"
#include <cstdio>
namespace raw::present {
namespace {
int differing(const RunResult& a, const RunResult& b){
    if (a.frames.size() != b.frames.size()) return -1;
    int n = 0;
    for (size_t i = 0; i < a.frames.size(); ++i){
        const FrameRecord &x = a.frames[i], &y = b.frames[i];
        n += x.frame != y.frame || x.width != y.width || x.height != y.height || x.hash != y.hash || x.hash.empty();
    }
    return n;
}
bool report(const char* what, bool ok, const std::string& detail){
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    return ok;
}
}  // namespace

int selftest(){
    RunOptions o; o.frames = 30;
    RunResult off, pres, ctl, offL, presL;
    int rc = runOffscreen(o, off);
    if (rc == kSkip){ std::printf("SKIP: no D3D12 adapter: %s\n", off.error.c_str()); return kSkip; }
    if (rc != kOk) return report("offscreen run", false, off.error), kRuntime;
    std::printf("adapter: %s\n", off.adapter.c_str());
    o.fullscreenCheck = true;
    rc = runPresented(o, pres);
    if (rc == kSkip){ std::printf("SKIP: no window or swapchain: %s\n", pres.error.c_str()); return kSkip; }
    bool ok = report("presented run", rc == kOk || rc == kCheckFailed, pres.error);
    ok &= report("presented frames equal offscreen frames", differing(pres, off) == 0, std::to_string(differing(pres, off)) + " of 30 differ");
    ok &= report("the frame graph owns the present", pres.graph.find("present sync 1") != std::string::npos, "");
    ok &= report("fullscreen and back", pres.fullscreenOk, pres.events.empty() ? "" : pres.events.back());
    o.fullscreenCheck = false; o.control = true; o.frames = 5;
    if (runPresented(o, ctl) != kOk) return report("control run", false, ctl.error), kRuntime;
    RunResult off5 = off; off5.frames.resize(5);
    ok &= report("control (blit offset 1) differs on every frame", differing(ctl, off5) == 5, std::to_string(differing(ctl, off5)) + " of 5 differ");
    RunOptions l; l.frames = 60; l.schedule.on = true;
    l.schedule.resizeA = 20; l.schedule.resizeB = 40; l.schedule.minimise = 45; l.schedule.lose = 50;
    if (runOffscreen(l, offL) != kOk) return report("offscreen lifecycle run", false, offL.error), kRuntime;
    rc = runPresented(l, presL);
    std::string ev; for (const std::string& e : presL.events) ev += "\n    " + e;
    ok &= report("lifecycle run (resize, minimise, device removal)", rc == kOk, presL.error + ev);
    ok &= report("lifecycle frames equal offscreen frames", differing(presL, offL) == 0, std::to_string(differing(presL, offL)) + " of 60 differ");
    ok &= report("one device loss, one recovery", presL.recoveries == 1, std::to_string(presL.recoveries));
    ok &= report("HDR10 signal within one code, control outside", hdrCommand("", false) == kOk, "");
    return ok ? kOk : kCheckFailed;
}
}  // namespace raw::present
