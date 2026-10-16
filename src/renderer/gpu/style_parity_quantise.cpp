// Checks of hysteresis quantisation and palette-locked lighting: bounds and controls of
// evidence/cpp-port-style-bounds-3.json, written before the first run.
#include "raw/renderer/style_parity.hpp"
#include "style_fixture.hpp"
#include "style_gpu_quantise.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gpu_check {
namespace {
using assets::Json;
namespace fx = style_fx;
using style::V3;
constexpr double kTie = 1e-9;
constexpr long kMaxExcused = 2;

double num(const Json& j, const char* k) { const Json* v = j.get(k); if (!v) throw assets::AssetError(std::string("fixture: missing ") + k); return v->number(k); }
struct Codes { long n{0}, over2{0}; double mean{0}; int max{0}; };
Codes codes(const std::vector<std::uint8_t>& want, const std::vector<std::uint32_t>& got, Codes c = {}) {
    double sum = c.mean * double(c.n);
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) {
            const int d = std::abs(int(want[i * 4 + k]) - int((got[i] >> (8 * k)) & 255u));
            sum += d; c.n++; c.max = std::max(c.max, d); if (d > 2) c.over2++;
        }
    c.mean = c.n ? sum / double(c.n) : 0;
    return c;
}
void gpuStats(StyleCase& s, const Codes& st) {
    s.gpuMismatchFraction = double(s.gpuMismatch) / double(s.decisions);
    s.gpuOver2Fraction = double(st.over2) / double(st.n); s.gpuMean = st.mean; s.gpuMax = st.max;
}

struct Frame { std::vector<float> rgba, dist, mv, dA; };
void hysteresisCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "hysteresis"; s.id = c.get("id")->s;
        const int w = int(num(c, "width")), h = int(num(c, "height")), F = int(num(c, "frames"));
        const std::string seq = c.get("sequence")->s;
        style::HystPlan p = style::hystPreset(c.get("preset")->s);
        const Json& q = *c.get("p");
        s.presetEqual = p.margin == num(q, "margin") && p.gain == num(q, "gain") && p.depthTol == num(q, "depthTol") && p.K == int(num(c, "K")) &&
                        p.bands == (q.get("mode")->s == "bands") && p.pal.size() == c.get("pal")->a.size();
        if (p.bands) s.presetEqual = s.presetEqual && p.N == int(num(q, "N"));
        for (std::size_t k = 0; k < p.pal.size() && s.presetEqual; ++k)
            for (std::size_t e = 0; e < 3; ++e) s.presetEqual = s.presetEqual && std::abs(p.pal[k].lab[e] - c.get("pal")->at(k).at(e).number("lab")) <= 1e-12;
        std::vector<Frame> frames;
        for (int k = 0; k < F; ++k) {
            const std::string pre = seq + "." + std::to_string(k) + ".";
            Frame f{b.arr<float>(pre + "rgba", "f32"), b.arr<float>(pre + "dist", "f32"), {}, {}};
            if (c.get("motion")->at(std::size_t(k)).b) { f.mv = b.arr<float>(pre + "mv", "f32"); f.dA = b.arr<float>(pre + "dA", "f32"); }
            frames.push_back(std::move(f));
        }
        // Addendum A of evidence/cpp-port-style-bounds-3.json: the margin set to 0 (hysteresis off).
        style::HystPlan bent = p; bent.margin = 0;
        style::HystState st, stb;
        std::vector<style::HystFrame> ref, alt;
        long controlDiff = 0;
        for (int k = 0; k < F; ++k) {
            const Frame& f = frames[std::size_t(k)];
            ref.push_back(style::hystStep(p, w, h, f.rgba, f.mv, f.dA, f.dist, st));
            alt.push_back(style::hystStep(bent, w, h, f.rgba, f.mv, f.dA, f.dist, stb));
            const auto jsIdx = b.arr<std::uint32_t>(s.id + "." + std::to_string(k) + ".js_idx", "u32");
            const auto jsRgba = b.arr<std::uint8_t>(s.id + "." + std::to_string(k) + ".js_rgba", "u8");
            const style::HystFrame& R = ref.back();
            for (std::size_t i = 0; i < jsIdx.size(); ++i) {
                s.decisions++;
                if (alt.back().idx[i] != std::int32_t(jsIdx[i])) controlDiff++;
                if (R.idx[i] != std::int32_t(jsIdx[i])) { (R.margin[i] <= kTie ? s.cpuExcused : s.cpuMismatch)++; continue; }
                for (std::size_t e = 0; e < 3; ++e)
                    if (R.rgba[i * 4 + e] != jsRgba[i * 4 + e]) { (R.margin[i] <= kTie ? s.cpuExcused : s.cpuRgbaMismatch)++; break; }
            }
        }
        s.cpuControlFails = controlDiff > 0;
        s.cpuPass = s.presetEqual && s.decisions == long(w) * h * F && s.cpuMismatch == 0 && s.cpuRgbaMismatch == 0 && s.cpuExcused <= kMaxExcused && s.cpuControlFails;
        if (dev) {
            style_gpu::GpuHysteresis g(*dev, p, w, h);
            Codes cs;
            long bentDiff = 0;
            for (int k = 0; k < F; ++k) {
                const Frame& f = frames[std::size_t(k)];
                std::vector<std::int32_t> idx; std::vector<std::uint32_t> packed;
                if (!g.frame(f.rgba, f.mv, f.dA, f.dist, idx, packed, r.error)) { r.cases.push_back(s); return; }
                for (std::size_t i = 0; i < idx.size(); ++i) { s.gpuMismatch += idx[i] != ref[std::size_t(k)].idx[i]; bentDiff += idx[i] != alt[std::size_t(k)].idx[i]; }
                cs = codes(ref[std::size_t(k)].rgba, packed, cs);
            }
            s.gpuRun = true;
            gpuStats(s, cs);
            s.gpuControlFails = double(bentDiff) / double(s.decisions) > 2e-4;
            s.gpuPass = s.gpuMismatchFraction <= 2e-4 && s.gpuOver2Fraction <= 2e-4 && s.gpuMean <= 0.05 && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
}

bool samePalette(const style::RampPalette& p, const Json& j) {
    const Json& rgb = *j.get("rgb");
    if (rgb.a.size() != p.entries.size()) return false;
    for (std::size_t k = 0; k < p.entries.size(); ++k)
        for (std::size_t c = 0; c < 3; ++c) if (p.entries[k].rgb[c] != int(rgb.at(k).at(c).number("rgb"))) return false;
    const Json& ramp = *j.get("ramp");
    const char* names[4] = {"ground", "stone", "wood", "foliage"};
    auto same = [](const std::vector<int>& v, const Json& a) {
        if (v.size() != a.a.size()) return false;
        for (std::size_t k = 0; k < v.size(); ++k) if (v[k] != int(a.at(k).number("ramp"))) return false;
        return true;
    };
    for (int c = 0; c < 4; ++c) for (int v = 0; v < 2; ++v) if (!same(p.ramp[c][v], ramp.get(names[c])->at(std::size_t(v)))) return false;
    return same(p.voidRamp, *ramp.get("void")) && same(p.window, *ramp.get("window")) && same(p.lamp, *ramp.get("lamp")) && p.night == int(num(ramp, "night"));
}
bool sameLight(const style::DitherLight& L, const Json& j) {
    return L.pos == fx::v3(*j.get("pos")) && L.radius == num(j, "radius") && L.core == num(j, "core") && L.intensity == num(j, "intensity") && L.wrap == num(j, "wrap");
}

void palettelightCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "palettelight"; s.id = c.get("id")->s;
        const Json& ov = *c.get("overrides");
        style::PalettelightPlan p = style::palettelightPlan(fx::v3(*c.get("lamp")), ov.get("steps") ? int(num(ov, "steps")) : 4, 6);
        if (ov.get("gain")) p.gain = num(ov, "gain");
        if (ov.get("moon")) p.moon = num(ov, "moon");
        const Json& q = *c.get("plan");
        // The moon direction is normalised with hypot, whose last bit differs between C libraries
        // (the WebAssembly build's musl against MSVC's and V8's), so it is compared within 1e-15.
        const V3 md = fx::v3(*q.get("moonDir"));
        const bool scalars = p.gain == num(q, "gain") && p.ambient == num(q, "ambient") && p.moon == num(q, "moon") && p.cycleRate == num(q, "cycleRate") &&
                             p.steps == int(num(q, "steps")) && p.voidSteps == int(num(q, "voidSteps"));
        const bool moon = std::abs(p.moonDir[0] - md[0]) <= 1e-15 && std::abs(p.moonDir[1] - md[1]) <= 1e-15 && std::abs(p.moonDir[2] - md[2]) <= 1e-15;
        const bool lights = sameLight(p.lamp, *q.get("lamp")) && sameLight(p.glows[0], q.get("glows")->at(0)) && sameLight(p.glows[1], q.get("glows")->at(1));
        const bool palette = samePalette(p.palette, *c.get("palette"));
        s.presetEqual = scalars && moon && lights && palette;
        if (!s.presetEqual) s.presetDetail = std::string(scalars ? "" : "scalars ") + (moon ? "" : "moonDir ") + (lights ? "" : "lights ") + (palette ? "" : "palette");
        style::ChapelGBuffer g;
        g.w = int(num(c, "width")); g.h = int(num(c, "height"));
        const std::string pre = s.id + ".";
        g.hit = b.arr<std::uint8_t>(pre + "hit", "u8"); g.cls = b.arr<std::uint8_t>(pre + "cls", "u8"); g.a = b.arr<float>(pre + "a", "f32");
        g.phase = b.arr<float>(pre + "phase", "f32"); g.P = b.arr<float>(pre + "P", "f32"); g.N = b.arr<float>(pre + "N", "f32"); g.vis = b.arr<float>(pre + "vis", "f32");
        const double time = num(c, "time");
        const style::PalettelightFrame f = style::runPalettelight(p, g, time);
        style::PalettelightPlan bent = p; bent.gain *= 1.1;
        const style::PalettelightFrame fb = style::runPalettelight(bent, g, time);
        const auto jsIdx = b.arr<std::uint32_t>(pre + "js_idx", "u32");
        const auto jsRgba = b.arr<std::uint8_t>(pre + "js_rgba", "u8");
        long controlDiff = 0;
        for (std::size_t i = 0; i < jsIdx.size(); ++i) {
            s.decisions++;
            controlDiff += fb.idx[i] != std::int32_t(jsIdx[i]);
            if (f.idx[i] != std::int32_t(jsIdx[i])) { (f.margin[i] <= kTie ? s.cpuExcused : s.cpuMismatch)++; continue; }
            for (std::size_t e = 0; e < 4; ++e) if (f.rgba[i * 4 + e] != jsRgba[i * 4 + e]) { s.cpuRgbaMismatch++; break; }
        }
        s.cpuControlFails = controlDiff > 0;
        s.cpuPass = s.presetEqual && s.decisions == long(g.w) * g.h && s.cpuMismatch == 0 && s.cpuRgbaMismatch == 0 && s.cpuExcused <= kMaxExcused && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> idx, packed;
            if (!style_gpu::runPalettelight(*dev, p, g, time, idx, packed, r.error)) { r.cases.push_back(s); return; }
            s.gpuRun = true;
            long bentDiff = 0;
            for (std::size_t i = 0; i < idx.size(); ++i) { s.gpuMismatch += std::int32_t(idx[i]) != f.idx[i]; bentDiff += std::int32_t(idx[i]) != fb.idx[i]; }
            gpuStats(s, codes(f.rgba, packed));
            s.gpuControlFails = double(bentDiff) / double(s.decisions) > 5e-4;
            s.gpuPass = s.gpuMismatchFraction <= 5e-4 && s.gpuOver2Fraction <= 5e-4 && s.gpuMean <= 0.15 && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
}
}  // namespace

// The hysteresis and palettelight cases; returns the number of cases their bundles hold.
int quantiseParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    using Fn = void (*)(rhi::Device*, const fx::Bundle&, StyleParity&);
    const std::pair<const char*, Fn> passes[] = {{"hysteresis", hysteresisCases}, {"palettelight", palettelightCases}};
    int expect = 0;
    for (const auto& [name, fn] : passes) {
        fx::Bundle b;
        if (!fx::loadBundle(dir + "/" + name + ".rnfx", b, r.error)) return expect;
        expect += int(b.head.get("cases")->a.size());
        fn(dev, b, r);
        if (!r.error.empty()) return expect;
    }
    return expect;
}

}  // namespace raw::gpu_check
