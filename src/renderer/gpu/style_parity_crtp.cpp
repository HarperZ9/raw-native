// The physical CRT's checks and its GPU runner: bounds and controls of
// evidence/cpp-port-style-bounds-12.json (with Addendum A), written before the first run.
#include "raw/renderer/style_crt.hpp"
#include "raw/renderer/style_parity.hpp"
#include "style_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
bool runCrtGpu(rhi::Device& dev, const style::CrtPlan& plan, const std::vector<float>& src, int frames, std::vector<std::uint32_t>& packed, std::string& err);   // style_parity_crtp_gpu.cpp
namespace {
using assets::Json;
namespace fx = style_fx;

struct Stats { int max{0}, p999{0}; double mean{0}; long differ{0}; };
Stats stats(const std::vector<std::uint8_t>& want, const std::vector<std::uint8_t>& got) {
    long hist[256] = {}, n = 0; double sum = 0; Stats s;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if ((i & 3) == 3) continue;
        const int d = std::abs(int(want[i]) - int(got[i]));
        hist[d]++; sum += d; n++; s.max = std::max(s.max, d); s.differ += d > 0;
    }
    s.mean = sum / double(n);
    long acc = 0;
    for (int d = 0; d < 256; ++d) { acc += hist[d]; if (double(acc) >= double(n) * 0.999) { s.p999 = d; break; } }
    return s;
}
bool gpuWithin(const Stats& s) { return s.max <= 3 && s.p999 <= 1 && s.mean <= 0.05; }
std::vector<std::uint8_t> codesOf(const std::vector<float>& img) {
    std::vector<std::uint8_t> o(img.size());
    for (std::size_t i = 0; i < img.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::crtEncode8(img[i]);
    return o;
}
double maxAbs(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 1e30;
    double m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(double(a[i]) - double(b[i])));
    return m;
}
// |a - b| <= 1e-5 |b| + 1e-12 on every value (the floor only for exact zeros).
bool withinRel(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(double(a[i]) - double(b[i])) > 1e-5 * std::abs(double(b[i])) + 1e-12) return false;
    return true;
}
style::CrtParams paramsFor(const Json& c) {
    style::CrtParams p = style::crtPreset(c.get("preset")->s);
    const Json& ov = *c.get("overrides");
    if (const Json* s = ov.get("signal")) if (const Json* st = s->get("standard")) p.pal = st->s == "pal";
    if (const Json* v = ov.get("interlace")) p.interlace = v->b;
    if (const Json* v = ov.get("view")) if (const Json* z = v->get("zoom")) p.zoom = z->number("zoom");
    if (const Json* cv = ov.get("convergence")) {
        if (const Json* r = cv->get("r")) { p.convR[0] = r->at(0).number("r"); p.convR[1] = r->at(1).number("r"); }
        if (const Json* b = cv->get("b")) { p.convB[0] = b->at(0).number("b"); p.convB[1] = b->at(1).number("b"); }
        if (const Json* k = cv->get("radial")) p.radial = k->number("radial");
    }
    if (const Json* m = ov.get("mask")) {
        if (const Json* l = m->get("lock")) p.pixelLock = l->s == "pixel";
        if (const Json* v = m->get("pitchPx")) p.pitchPx = v->number("pitchPx");
        if (const Json* v = m->get("mix")) p.mix = v->number("mix");
    }
    return p;
}
std::string planDiff(const style::CrtPlan& plan, const fx::Bundle& b, const std::string& id) {
    std::string d;
    const std::vector<double> want = b.arr<double>(id + ".params", "f64"), got = style::crtParamBlock(plan, 0);
    bool ok = want.size() == got.size();
    for (std::size_t i = 0; ok && i < want.size(); ++i) ok = std::abs(got[i] - want[i]) <= std::max(1e-12 * std::abs(want[i]), 1e-15);
    if (!ok) d += "params ";
    const std::vector<double> wt = b.arr<double>(id + ".taps", "f64");
    // Addendum B: within 1e-12 of the largest tap (a notch filter's near-zero taps are residues of
    // a cancellation, where 1e-12 relative is below float64 rounding).
    double big = 0;
    for (double v : wt) big = std::max(big, std::abs(v));
    ok = wt.size() == plan.taps64.size();
    for (std::size_t i = 0; ok && i < wt.size(); ++i) ok = std::abs(plan.taps64[i] - wt[i]) <= 1e-12 * big;
    if (!ok) d += "taps ";
    const std::vector<float> wh = b.arr<float>(id + ".halo", "f32");
    ok = wh.size() == plan.halo.weights.size();
    for (std::size_t i = 0; ok && i < wh.size(); ++i) {
        std::int32_t x, y;
        std::memcpy(&x, &wh[i], 4); std::memcpy(&y, &plan.halo.weights[i], 4);
        ok = std::abs(long(x) - long(y)) <= 1;
    }
    if (!ok) d += "halo ";
    return d;
}
}  // namespace

// The crt cases; returns the number the bundle holds.
int crtParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/crt.rnfx", b, r.error)) return 0;
    const std::vector<float> src = b.arr<float>("src", "f32");
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "crt"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("w")), h = int(c.get("height")->number("h")), frames = int(c.get("frames")->number("frames"));
        const style::CrtParams p = paramsFor(c);
        style::CrtParams pb = p; pb.contrast *= 1.05;
        const style::CrtPlan plan = style::resolveCrt(p, 256, 224, w, h), bentPlan = style::resolveCrt(pb, 256, 224, w, h);
        s.presetDetail = planDiff(plan, b, s.id);
        s.presetEqual = s.presetDetail.empty();
        std::vector<float> hist(std::size_t(w) * h * 8, 0.0f), bhist = hist;
        style::CrtFrame fr, bf;
        for (int f = 0; f < frames; ++f) { fr = style::crtFrame(plan, src, hist, f); bf = style::crtFrame(bentPlan, src, bhist, f); }
        const std::vector<std::uint8_t> js = b.arr<std::uint8_t>(s.id + ".js", "u8"), codes = codesOf(fr.img), bcodes = codesOf(bf.img);
        const std::vector<float> jsImg = b.arr<float>(s.id + ".img", "f32"), jsSig = b.arr<float>(s.id + ".sig", "f32");
        const double sigMx = maxAbs(fr.sig, jsSig), mx = maxAbs(fr.img, jsImg), bmx = maxAbs(bf.img, jsImg);
        bool emOk = true;
        if (c.get("emission")->b) {
            emOk = withinRel(fr.em, b.arr<float>(s.id + ".em", "f32")) && withinRel(hist, b.arr<float>(s.id + ".hist", "f32"));
            if (!emOk) s.presetDetail += "emission or history ";
        }
        const Stats cs = stats(js, codes), cb = stats(js, bcodes);
        s.decisions = long(js.size() / 4 * 3);
        s.cpuMaxAbs = mx; s.cpuMismatch = cs.differ; s.cpuRgbaMismatch = cs.max;
        if (sigMx > 1e-6) s.presetDetail += "signal ";
        const bool cpuCodes = double(cs.differ) <= 1e-4 * double(s.decisions) && cs.max <= 1;
        s.cpuControlFails = bmx > 1e-5 || double(cb.differ) > 1e-4 * double(s.decisions) || cb.max > 1;
        s.cpuPass = s.presetEqual && sigMx <= 1e-6 && emOk && mx <= 1e-5 && cpuCodes && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runCrtGpu(*dev, plan, src, frames, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            std::vector<std::uint8_t> g8(got.size() * 4);
            std::memcpy(g8.data(), got.data(), g8.size());
            const Stats gs = stats(codes, g8), gb = stats(bcodes, g8);
            s.gpuMax = gs.max; s.gpuP999 = gs.p999; s.gpuMean = gs.mean;
            s.gpuControlFails = !gpuWithin(gb);
            s.gpuPass = gpuWithin(gs) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
