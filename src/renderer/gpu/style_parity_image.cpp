// The round 2 image passes' checks (purity, lightpaint, glaze, hatch): bounds and controls of
// evidence/cpp-port-style-bounds-2.json, image_passes, written before the first run.
#include "raw/renderer/style_parity.hpp"
#include "raw/renderer/style_image.hpp"
#include "style_fixture.hpp"
#include "style_gpu_image.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
namespace raw::gpu_check {
namespace {
using assets::Json;
namespace fx = style_fx;

constexpr double kTie = 1e-9, kValue = 1e-9;
constexpr long kMaxExcused = 2;
enum class Enc { Display, Lab };
std::uint8_t enc8(Enc e, double v) { return e == Enc::Display ? style::encodeDisplay8(v) : style::encodeLab8(v); }
double edge8(Enc e, double v) { return e == Enc::Display ? style::edgeDisplay8(v) : style::edgeLab8(v); }
std::vector<std::uint8_t> codesOf(Enc e, const std::vector<double>& v) {
    std::vector<std::uint8_t> o(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) o[i] = (i & 3) == 3 ? 255 : enc8(e, v[i]);
    return o;
}
double maxAbs(const std::vector<double>& a, const std::vector<double>& b) {
    double m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if ((i & 3) != 3) m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}
// codeStats of web/shaders/lab/gpu-kit.mjs over RGB, the reference's codes against packed words.
struct Stats { int max{0}, p999{0}; double mean{0}, over2{0}; };
Stats stats(const std::vector<std::uint8_t>& want, const std::vector<std::uint32_t>& got) {
    long hist[256] = {}, n = 0; double sum = 0; int mx = 0;
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) {
            const int d = std::abs(int(want[i * 4 + k]) - int((got[i] >> (8 * k)) & 255u));
            hist[d]++; sum += d; n++; mx = std::max(mx, d);
        }
    Stats s; s.max = mx; s.mean = n ? sum / double(n) : 0; s.over2 = n ? double(n - hist[0] - hist[1] - hist[2]) / double(n) : 0;
    long acc = 0;
    for (int d = 0; d < 256; ++d) { acc += hist[d]; if (double(acc) >= double(n) * 0.999) { s.p999 = d; break; } }
    return s;
}
bool withinGpu(const std::string& pass, const Stats& s) {
    if (pass == "hatch") return s.over2 <= 0.005 && s.mean <= 0.5;
    return s.max <= 2 && s.p999 <= 1 && s.mean <= 0.05;
}

// The CPU link of one case, then (with a device) the GPU link. run(control) is the C++ reference,
// control true with the case's built-in perturbation; gpu fills the packed words.
void evaluate(StyleCase& s, Enc e, const std::vector<double>& js, const std::function<std::vector<double>(bool)>& run,
              const std::function<bool(std::vector<std::uint32_t>&, std::string&)>& gpu, rhi::Device* dev, StyleParity& r) {
    const std::vector<double> cpp = run(false), bent = run(true);
    s.decisions = long(js.size() / 4);
    s.cpuMaxAbs = maxAbs(cpp, js);
    for (std::size_t i = 0; i < js.size(); ++i) {
        if ((i & 3) == 3 || enc8(e, cpp[i]) == enc8(e, js[i])) continue;
        (edge8(e, cpp[i]) <= kTie ? s.cpuExcused : s.cpuMismatch)++;
    }
    s.cpuControlFails = maxAbs(bent, js) > kValue;
    s.cpuPass = s.presetEqual && s.cpuMaxAbs <= kValue && s.cpuMismatch == 0 && s.cpuExcused <= kMaxExcused && s.hitMismatch == 0 &&
                s.fallbackMismatch == 0 && s.cpuControlFails;
    if (!dev) return;
    std::vector<std::uint32_t> got;
    if (!gpu(got, r.error)) return;
    s.gpuRun = true;
    const Stats st = stats(codesOf(e, cpp), got), sc = stats(codesOf(e, bent), got);
    s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean; s.gpuOver2Fraction = st.over2;
    s.gpuControlFails = !withinGpu(s.pass, sc);
    s.gpuPass = withinGpu(s.pass, st) && s.gpuControlFails;
}

double num(const Json& j, const char* k) { const Json* v = j.get(k); if (!v) throw assets::AssetError(std::string("fixture: missing ") + k); return v->number(k); }
template<std::size_t N> bool sameArr(const std::array<double, N>& a, const Json& j) {
    for (std::size_t k = 0; k < N; ++k) if (a[k] != j.at(k).number("element")) return false;
    return true;
}

void purityCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "purity"; s.id = c.get("id")->s;
        const int w = int(num(c, "width")), h = int(num(c, "height"));
        const double t = num(c, "t");
        const style::PurityPlan p = style::purityPreset(c.get("preset")->s);
        const Json& q = *c.get("p");
        s.presetEqual = p.s == num(q, "s") && p.spread == num(q, "spread") && p.w == num(q, "w") && p.mag == num(q, "mag") && p.magScale == num(q, "magScale") &&
                        p.earth == num(q, "earth") && p.degauss == num(q, "degauss") && p.tau == num(q, "tau") && p.mains == num(q, "mains") &&
                        p.field == num(q, "field") && p.blank == num(q, "blank") && p.wobble == num(q, "wobble");
        const auto src = b.arr<float>(s.id + ".src", "f32");
        style::PurityPlan bent = p; bent.s = 0.19;
        evaluate(s, Enc::Display, b.arr<double>(s.id + ".js", "f64"), [&](bool k) { return style::runPurity(k ? bent : p, w, h, src, t); },
                 [&](std::vector<std::uint32_t>& got, std::string& err) { return style_gpu::runPurity(*dev, p, w, h, src, t, got, err); }, dev, r);
        r.cases.push_back(s);
    }
}

style::LitFrame litFrame(const fx::Bundle& b, const std::string& id, const Json& c) {
    style::LitFrame f;
    f.w = int(num(c, "width")); f.h = int(num(c, "height"));
    f.albedo = b.arr<float>(id + ".albedo", "f32"); f.light = b.arr<float>(id + ".light", "f32"); f.normals = b.arr<float>(id + ".normals", "f32");
    f.fogT = b.arr<float>(id + ".fogT", "f32"); f.depth = b.arr<float>(id + ".depth", "f32"); f.kind = b.arr<std::uint8_t>(id + ".kind", "u8");
    f.fogColor = fx::v3(*c.get("fogColor"));
    if (const Json* cam = c.get("camera")) {
        f.camera.eye = fx::v3(*cam->get("eye")); f.camera.fwd = fx::v3(*cam->get("fwd")); f.camera.right = fx::v3(*cam->get("right"));
        f.camera.up = fx::v3(*cam->get("up")); f.camera.tan = num(*cam, "tan"); f.camera.aspect = num(*cam, "aspect");
    }
    return f;
}

void lightpaintCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "lightpaint"; s.id = c.get("id")->s;
        const style::LitFrame f = litFrame(b, s.id, c);
        const style::LightpaintPlan p = style::lightpaintPreset(c.get("preset")->s);
        const Json& q = *c.get("p");
        s.presetEqual = sameArr(p.shadow, *q.get("shadow")) && sameArr(p.light, *q.get("light")) && p.Emid == num(q, "Emid") && p.t0 == num(q, "t0") &&
                        p.strength == num(q, "strength") && p.shape == num(q, "shape") && p.complement == q.get("complement")->b;
        style::LightpaintPlan bent = p; bent.Emid *= 1.25;
        evaluate(s, Enc::Lab, b.arr<double>(s.id + ".js", "f64"), [&](bool k) { return style::runLightpaint(k ? bent : p, f); },
                 [&](std::vector<std::uint32_t>& got, std::string& err) { return style_gpu::runLightpaint(*dev, p, f, 2.4, got, err); }, dev, r);
        r.cases.push_back(s);
    }
}

void glazeCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "glaze"; s.id = c.get("id")->s;
        const style::LitFrame f = litFrame(b, s.id, c);
        const style::GlazePlan p = style::glazePreset(c.get("preset")->s);
        const Json& q = *c.get("p");
        s.presetEqual = p.d0 == num(q, "d0") && p.dVar == num(q, "dVar") && p.noiseScale == num(q, "noiseScale") && p.coverage == num(q, "coverage") &&
                        p.n0 == num(q, "n0") && p.n2 == num(q, "n2") && p.A == num(q, "A") && p.B == num(q, "B") && p.dispersion == q.get("dispersion")->b &&
                        p.filmN == num(q, "filmN");
        style::GlazePlan bent = p; bent.coverage -= 0.1;
        evaluate(s, Enc::Lab, b.arr<double>(s.id + ".js", "f64"), [&](bool k) { return style::runGlaze(k ? bent : p, f); },
                 [&](std::vector<std::uint32_t>& got, std::string& err) { return style_gpu::runGlaze(*dev, p, f, 2.4, got, err); }, dev, r);
        r.cases.push_back(s);
    }
}

void hatchCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "hatch"; s.id = c.get("id")->s;
        const int w = int(num(c, "width")), h = int(num(c, "height"));
        const style::HatchPlan p = style::hatchPreset(c.get("preset")->s);
        const Json& q = *c.get("p");
        s.presetEqual = p.freq == num(q, "freq") && p.width == num(q, "width") && p.layers == int(num(q, "layers")) && p.umb == num(q, "umb") &&
                        p.contour == num(q, "contour") && sameArr(p.ink, *q.get("ink")) && sameArr(p.paper, *q.get("paper"));
        const style::HatchFrame hf = style::runHatch(p, w, h);
        const auto jsHit = b.arr<std::uint8_t>(s.id + ".js_hit", "u8"), jsFb = b.arr<std::uint8_t>(s.id + ".js_fallback", "u8");
        for (std::size_t i = 0; i < jsHit.size(); ++i) {
            if (hf.hit[i] != jsHit[i]) { s.hitMismatch++; continue; }
            if (hf.hit[i] && hf.fallback[i] != jsFb[i]) s.fallbackMismatch++;
        }
        style::HatchPlan bent = p; bent.freq += 1;
        evaluate(s, Enc::Display, b.arr<double>(s.id + ".js", "f64"), [&](bool k) { return k ? style::runHatch(bent, w, h).rgba : hf.rgba; },
                 [&](std::vector<std::uint32_t>& got, std::string& err) { return style_gpu::runHatch(*dev, p, w, h, got, err); }, dev, r);
        r.cases.push_back(s);
    }
}
}  // namespace

// Runs the round 2 image cases; returns the number of cases the bundles hold.
int imageParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    using Fn = void (*)(rhi::Device*, const fx::Bundle&, StyleParity&);
    const std::pair<const char*, Fn> passes[] = {{"purity", purityCases}, {"lightpaint", lightpaintCases}, {"glaze", glazeCases}, {"hatch", hatchCases}};
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
