// raw_native_cli art tiltshift --gpu / --cost: g1 (parity against the float64 reference) and g3
// (the chain's GPU time at 1080p) of evidence/tiltshift-bounds.json.
#include "art_report.hpp"
#include "raw/renderer/art_tiltshift_gpu.hpp"
#include "raw/renderer/style_paint.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::art_cmd {
namespace {
using namespace raw::art;
struct Case { const char* name; bool town; const char* look; };
struct Setup { View v; Frame f; LensGeom g; FxPlan P; std::vector<float> paint; };
Setup setup(const Case& c, int w, int h, int threads, double kScale) {
    Setup s;
    s.v = c.town ? townView(w, h) : orbitView(0, w, h);
    s.f = c.town ? renderFrame(s.v, townDusk(), threads, SceneKind::Town) : renderFrame(s.v, daylight(), threads);
    TiltLook L = tiltLook(c.look);
    L.K *= kScale;
    const D3 focus = c.town ? D3{0, 0.89, 0} : D3{0, 1, 0};
    s.g = resolveLens(lensOf(L, focus, {1, 0, 0}), s.v);
    s.P = planFx(L, {3, false, false}, w, h, s.v.focalPx());
    s.paint = style::runPaint(style::resolvePaint(style::paintPreset("oil"), w, h), s.f.lit, nullptr);
    return s;
}
std::vector<std::uint8_t> cpuCodes(const Setup& s, int threads) {
    return codes8(fxStack(s.P, paintDefocus(s.paint, cocMap(s.f, s.g), s.f.w, s.f.h, {}, DefocusMode::Paint, threads)));
}
std::string parity(rhi::Device& dev, const Options& o, const Case& c, bool& pass) {
    const int w = 480, h = 270;
    const Setup s = setup(c, w, h, o.threads, 1.0), bent = setup(c, w, h, o.threads, 1.1);
    gpu_check::TiltGpu g;
    Json j;
    j.str("case", c.name).str("look", c.look);
    if (!gpu_check::tiltOnGpu(dev, s.f, s.g, s.P, s.paint, g, 0)) { pass = false; return j.str("error", g.error).done(); }
    const std::vector<float> coc = cocMap(s.f, s.g);
    double dmax = 0;
    for (std::size_t i = 0; i < g.coc.size(); ++i) dmax = std::max(dmax, std::abs(double(g.coc[i]) - coc[i * 2]));
    const auto got = unpack(g.packed);
    const CodeStats st = codeStats(cpuCodes(s, o.threads), got), ct = codeStats(cpuCodes(bent, o.threads), got);
    auto within = [](const CodeStats& x) { return x.max <= 16 && x.over8 * 10000 <= x.n && x.p999 <= 3 && x.mean <= 0.2; };   // addendum E
    const bool ok = dmax <= 0.01 && within(st) && !within(ct);
    pass = pass && ok;
    writeCodesPng(o.out + "/gpu-" + c.name + ".png", w, h, got);
    std::printf("art tiltshift gpu %s: coc %.2e, codes max %d p999 %d mean %.4f, control max %d -> %d\n", c.name, dmax, st.max, st.p999, st.mean, ct.max, ok);
    return j.num("coc_max_abs", dmax).raw("codes", statsJson(st)).raw("control_k_1_1", statsJson(ct)).boolean("control_fails", !within(ct)).boolean("pass", ok).done();
}
std::string cost(rhi::Device& dev, const Options& o, bool& pass) {
    const int w = 1920, h = 1080;
    Setup s = setup({"town-1080", true, "town_map"}, w, h, o.threads, 1.0);
    TiltLook L = s.P.look;   // every effect on
    L.halation = 1; L.star = 1; L.ca = 1; L.grain = 0.03; L.weave = 0.5; L.print = 0.3; L.misreg = 1; L.halftone = 0.3; L.mech = 0.3;
    s.P = planFx(L, {3, false, false}, w, h, s.v.focalPx());
    gpu_check::TiltGpu g;
    Json j;
    j.integer("width", w).integer("height", h).integer("repeats", o.repeats).str("look", "town_map, every effect on");
    if (!gpu_check::tiltOnGpu(dev, s.f, s.g, s.P, s.paint, g, o.repeats)) { pass = false; return j.str("error", g.error).done(); }
    const bool ok = g.chainMs <= 1.0;
    pass = pass && ok;
    std::printf("art tiltshift gpu: chain %.3f ms at 1080p\n", g.chainMs);
    static const char* const names[] = {"coc", "weight", "half", "prep", "tensor", "blur_x", "blur_y", "defocus", "compose", "bright", "glow", "finish"};
    std::string list = "art tiltshift gpu: passes";
    Json per;
    for (std::size_t k = 0; k < g.stepMs.size() && k < 12; ++k) { char b[64]; std::snprintf(b, sizeof b, " %s %.3f", names[k], g.stepMs[k]); list += b; per.num(names[k], g.stepMs[k]); }
    std::printf("%s\n", list.c_str());
    return j.num("chain_ms", g.chainMs).raw("pass_ms", per.done()).boolean("g3_pass", ok).done();
}
}  // namespace

std::string tiltGpu(const Options& o, bool& pass) {
    std::string why;
    rhi::Device* dev = rhi::device(why);
    Json j;
    if (!dev) { pass = false; return j.str("error", "no GPU backend or adapter: " + why).done(); }
    j.str("backend", dev->backendName()).str("adapter", dev->adapter().description);
    if (o.gpu) {
        std::string cases = "[";
        for (const Case& c : {Case{"town-map", true, "town_map"}, Case{"town-night", true, "night"}, Case{"art-daily", false, "daily"}, Case{"town-dream", true, "dream"}})
            cases += (cases.size() > 1 ? "," : "") + parity(*dev, o, c, pass);
        j.raw("g1_cases", cases + "]");
    }
    if (o.cost) j.raw("g3_cost", cost(*dev, o, pass));
    return j.done();
}

}  // namespace raw::art_cmd
