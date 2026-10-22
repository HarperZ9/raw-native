// raw_native_cli art strokes: P5's record against evidence/p5-world-strokes-bounds.json (w1 to w5).
#include "art_report.hpp"
#include "raw/renderer/art_strokes.hpp"
#include "raw/rhi/rhi.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::art_cmd {
namespace {
constexpr std::uint64_t kSeed = 20261022;
const char* const kPresets[] = {"oil", "watercolour"};

std::string goldens(const Options& o, bool& ok) {
    const art::View v = art::orbitView(0, 256, 144);
    const art::Frame f = art::renderFrame(v, art::daylight(), o.threads);
    const art::AnchorParams a = art::anchorParams(v);
    const std::vector<float> chart = art::anchorCanvas(f, a);
    Json j;
    j.str("anchor_fnv", hex(art::fnv1a(chart)));
    ok = writeCodesPng(o.out + "/lit.png", f.w, f.h, art::paintReference(f.lit, 2.2));
    for (const char* p : kPresets) {
        const auto codes = art::codes8(art::paintWorld(f, a, style::resolvePaint(style::paintPreset(p), f.w, f.h)));
        j.str((std::string(p) + "_fnv").c_str(), hex(art::fnv1a(codes)));
        ok = writeCodesPng(o.out + "/golden-" + p + ".png", f.w, f.h, codes) && ok;
    }
    return j.done();
}

std::string orbit(const Options& o, bool& pass) {
    Json j;
    for (const char* p : kPresets) {
        art::StrokeOrbit so;
        so.frames = o.frames; so.threads = o.threads; so.preset = p;
        const art::StrokeOrbitResult r = art::runStrokeOrbit(so, kSeed);
        const art::Paired& w = r.worldVsAdvected;
        const bool separates = w.diffHi < 0 || w.diffLo > 0;
        const bool w1 = w.reduction >= 0.40 && separates, w2 = r.shuffledVsAdvected.reduction < 0.40;
        pass = pass && w1 && w2;
        Json k;
        k.raw("world_vs_advected", pairedJson(w)).raw("shuffled_vs_advected", pairedJson(r.shuffledVsAdvected))
         .raw("offset_vs_advected", pairedJson(r.offsetVsAdvected))
         .boolean("w1_pass", w1).boolean("w1_kill", !separates).boolean("w2a_control_fails", w2)
         .nums("world", r.world).nums("advected", r.advected).nums("shuffled", r.shuffled).nums("offset", r.offset);
        j.raw(p, k.done());
        std::printf("art strokes: %s reduction %.3f [%.3f, %.3f], shuffled %.3f, offset %.3f\n", p, w.reduction, w.redLo, w.redHi,
                    r.shuffledVsAdvected.reduction, r.offsetVsAdvected.reduction);
    }
    return j.done();
}

std::string gpuCase(rhi::Device& dev, const Options& o, int frame, const char* preset, bool& pass) {
    const art::View v = art::orbitView(frame, 256, 144);
    const art::Frame f = art::renderFrame(v, art::daylight(), o.threads);
    const art::AnchorParams a = art::anchorParams(v);
    style::PaintParams pp = style::paintPreset(preset), bent = pp;
    bent.exposure *= 1.1;
    const auto want = art::codes8(art::paintWorld(f, a, style::resolvePaint(pp, f.w, f.h)));
    const auto wantBent = art::codes8(art::paintWorld(f, a, style::resolvePaint(bent, f.w, f.h)));
    const std::vector<float> chart = art::anchorCanvas(f, a);
    gpu_check::StrokesGpu g;
    Json j;
    j.integer("frame", frame).str("preset", preset);
    if (!gpu_check::strokesOnGpu(dev, f, a, style::resolvePaint(pp, f.w, f.h), g, 0)) { pass = false; return j.str("error", g.error).done(); }
    double dmax = 0;
    for (std::size_t i = 0; i < chart.size(); ++i) dmax = std::max(dmax, std::abs(double(chart[i]) - g.anchor[i]));
    const auto got = unpack(g.packed);
    const CodeStats s = codeStats(want, got), c = codeStats(wantBent, got);
    auto within = [](const CodeStats& x) { return x.max <= 8 && x.p999 <= 3 && x.mean <= 0.2; };
    const bool ok = dmax <= 0.02 && within(s) && !within(c);
    pass = pass && ok;
    if (frame == 0) writeCodesPng(o.out + "/gpu-" + preset + ".png", f.w, f.h, got);
    return j.num("anchor_max_abs", dmax).raw("paint", statsJson(s)).raw("control", statsJson(c)).boolean("control_fails", !within(c)).boolean("pass", ok).done();
}

std::string cost(rhi::Device& dev, const Options& o, bool& pass) {
    const art::View v = art::orbitView(0, 1920, 1080);
    const art::Frame f = art::renderFrame(v, art::daylight(), o.threads);
    gpu_check::StrokesGpu g;
    Json j;
    j.integer("width", 1920).integer("height", 1080).integer("repeats", o.repeats).str("preset", "oil");
    if (!gpu_check::strokesOnGpu(dev, f, art::anchorParams(v), style::resolvePaint(style::paintPreset("oil"), f.w, f.h), g, o.repeats)) {
        pass = false;
        return j.str("error", g.error).done();
    }
    pass = pass && g.anchorMs <= 2.0;
    return j.num("anchor_ms", g.anchorMs).num("anchor_single_submit_ms", g.anchorSingleMs).integer("inner_repeats", 10).num("advected_canvas_ms", g.advectMs).num("paint_world_ms", g.paintWorldMs)
            .num("paint_advected_ms", g.paintAdvectedMs).boolean("w5_pass", g.anchorMs <= 2.0).done();
}
}  // namespace

int strokes(const Options& o) {
    bool pass = true, png = true;
    Json j;
    j.str("schema", "raw-native.evidence/1").str("bounds", "evidence/p5-world-strokes-bounds.json").raw("goldens", goldens(o, png));
    if (o.orbit) j.raw("orbit", orbit(o, pass));
    if (o.gpu || o.cost) {
        std::string why;
        rhi::Device* dev = rhi::device(why);
        if (!dev) { std::fprintf(stderr, "art strokes: no GPU backend or adapter: %s\n", why.c_str()); writeText(o.out + "/strokes.json", j.done()); return 4; }
        j.str("backend", dev->backendName()).str("adapter", dev->adapter().description);
        if (o.gpu) {
            std::string cases = "[";
            for (int frame : {0, 6}) for (const char* p : kPresets) cases += (cases.size() > 1 ? "," : "") + gpuCase(*dev, o, frame, p, pass);
            j.raw("gpu_cases", cases + "]");
        }
        if (o.cost) j.raw("cost", cost(*dev, o, pass));
    }
    j.boolean("pass", pass && png);
    const std::string doc = j.done();
    if (!writeText(o.out + "/strokes.json", doc)) return 2;
    std::printf("%s\n", doc.c_str());
    return pass && png ? 0 : 1;
}

}  // namespace raw::art_cmd
