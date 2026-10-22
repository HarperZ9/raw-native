// The courtyard orbit report of raw_native_cli fractal-dither: see raw/tools/fractal_cmd.hpp.
#include "raw/renderer/fractal_metrics.hpp"
#include "raw/tools/fractal_cmd.hpp"
#include <algorithm>
#include <cstdio>
namespace raw::fractal {
namespace {
using style::Anchor;

std::string fmt(const char* f, double a) { char b[96]; std::snprintf(b, sizeof b, f, a); return b; }

// Display codes of the undithered tone through the plan's ink and paper mix.
std::vector<std::uint8_t> refCodes(const style::DitherPlan& p, const style::DitherFrame& f) {
    std::vector<std::uint8_t> r(f.tone.size() * 4, 255);
    for (std::size_t i = 0; i < f.tone.size(); ++i)
        for (std::size_t c = 0; c < 3; ++c) r[i * 4 + c] = encode8(p.ink[c] + (p.paper[c] - p.ink[c]) * std::min(1.0, std::max(0.0, f.tone[i])));
    return r;
}
struct Variant {
    const char* name;
    Anchor anchor;
    double fractalDepth;   // control: scale term fixed
    bool shuffled;         // control: anchor taken from frame (7 t mod N)
};
const Variant kVariants[] = {{"screen", Anchor::Screen, 0, false}, {"fractal", Anchor::Fractal, 0, false},
                             {"world_octave", Anchor::WorldOctave, 0, false}, {"fractal_shuffled_anchor", Anchor::Fractal, 0, true},
                             {"fractal_scale_term_broken", Anchor::Fractal, 8.5, false}};

struct Series { std::vector<double> inst; double accuracy{0}; };

Series runVariant(const Variant& v, const std::string& preset, const std::vector<Frame>& fr, const std::vector<Motion>& mo, double px) {
    Series s;
    const int w = fr[0].w, h = fr[0].h, n = int(fr.size());
    style::DitherPlan plan = orbitPlan(preset, v.anchor, h, px);
    plan.fractalDepth = v.fractalDepth;
    std::vector<std::uint8_t> prevOut, prevRef;
    double accSum = 0;
    for (int t = 0; t < n; ++t) {
        style::DitherGBuffer g = fr[std::size_t(t)].g;
        if (v.shuffled) { const style::DitherGBuffer& o = fr[std::size_t((7 * t) % n)].g; g.apos = o.apos; g.anrm = o.anrm; }
        const style::DitherFrame f = style::runDitherlight(plan, lights(), g);
        const std::vector<std::uint8_t> ref = refCodes(plan, f);
        if (t > 0) s.inst.push_back(instability(f.rgba, prevOut, ref, prevRef, mo[std::size_t(t)].mv, mo[std::size_t(t)].valid, w, h));
        std::vector<double> lin(f.level.size());
        for (std::size_t i = 0; i < lin.size(); ++i) lin[i] = style::levelLinear(plan, f.level[i]);
        std::vector<std::uint8_t> surf(fr[std::size_t(t)].id.size());
        for (std::size_t i = 0; i < surf.size(); ++i) surf[i] = fr[std::size_t(t)].id[i] != 0;
        accSum += accuracyA(lin, f.tone, surf, w, h);
        prevOut = f.rgba; prevRef = ref;
    }
    s.accuracy = accSum / n;
    return s;
}
std::string pairJson(const Paired& p) {
    return "{\"mean_candidate\": " + fmt("%.6g", p.meanA) + ", \"mean_baseline\": " + fmt("%.6g", p.meanB) + ", \"diff\": " + fmt("%.6g", p.diff) +
           ", \"diff_lo\": " + fmt("%.6g", p.diffLo) + ", \"diff_hi\": " + fmt("%.6g", p.diffHi) + ", \"reduction\": " + fmt("%.4f", p.reduction) +
           ", \"reduction_lo\": " + fmt("%.4f", p.redLo) + ", \"reduction_hi\": " + fmt("%.4f", p.redHi) + "}";
}
}  // namespace

int orbitReport(int frames, int w, int h, double px, std::string& json) {
    const std::vector<Frame> fr = orbitFrames(frames, w, h);
    std::vector<Motion> mo(fr.size());
    for (std::size_t t = 1; t < fr.size(); ++t) mo[t] = motion(fr[t], fr[t - 1]);
    constexpr std::uint64_t kSeed = 20261022;
    bool ok = true;
    json = "{\n \"check\": \"fractal dither orbit (f1, f2, f8)\", \"frames\": " + std::to_string(frames) + ", \"size\": [" + std::to_string(w) + ", " + std::to_string(h) +
           "], \"px\": " + fmt("%g", px) + ", \"bootstrap_seed\": " + std::to_string(kSeed) + ", \"resamples\": 10000,\n \"presets\": {";
    bool firstPreset = true;
    for (const char* preset : {"ink", "tones4"}) {
        Series s[5];
        for (int k = 0; k < 5; ++k) s[k] = runVariant(kVariants[k], preset, fr, mo, px);
        const Paired fv = pairedBootstrap(s[1].inst, s[0].inst, kSeed), sv = pairedBootstrap(s[0].inst, s[0].inst, kSeed);
        const Paired sh = pairedBootstrap(s[3].inst, s[0].inst, kSeed), br = pairedBootstrap(s[4].inst, s[0].inst, kSeed), wo = pairedBootstrap(s[2].inst, s[0].inst, kSeed);
        const bool isInk = std::string(preset) == "ink";
        const bool f1 = fv.reduction >= 0.5, kill = isInk && fv.diffLo <= 0 && fv.diffHi >= 0;
        const bool ctl = sv.reduction < 0.5 && sh.reduction < 0.5;
        ok = ok && f1 && !kill && ctl;
        json += std::string(firstPreset ? "" : ",") + "\n  \"" + preset + "\": {\n   \"fractal_vs_screen\": " + pairJson(fv) + ",\n   \"f1_holds\": " + (f1 ? "true" : "false") +
                ", \"kill_fires\": " + (kill ? "true" : "false") + ",\n   \"control_screen_vs_screen\": " + pairJson(sv) + ",\n   \"control_shuffled_anchor\": " + pairJson(sh) +
                ",\n   \"control_scale_term_broken\": " + pairJson(br) + ",\n   \"controls_fail_as_required\": " + (ctl ? "true" : "false") + ",\n   \"recorded_world_octave\": " + pairJson(wo) +
                ",\n   \"accuracy_A\": {\"screen\": " + fmt("%.3f", s[0].accuracy) + ", \"fractal\": " + fmt("%.3f", s[1].accuracy) + ", \"world_octave\": " + fmt("%.3f", s[2].accuracy) + "}\n  }";
        firstPreset = false;
    }
    json += std::string("\n },\n \"pass\": ") + (ok ? "true" : "false") + "\n}\n";
    return ok ? 0 : 1;
}

}  // namespace raw::fractal
