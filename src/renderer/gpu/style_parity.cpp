// The style passes' checks: see raw/renderer/style_parity.hpp. Bounds and controls are those of
// evidence/cpp-port-style-bounds.json, written before the first run.
#include "raw/renderer/style_parity.hpp"
#include "style_fixture.hpp"
#include "style_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::gpu_check {
namespace {
using assets::Json;
namespace fx = style_fx;

constexpr double kTie = 1e-9;          // a float64 decision this close to its threshold is a tie
constexpr long kMaxExcused = 2;        // excused ties a case may have
struct GpuBounds { double mismatch, over2, mean; };
constexpr GpuBounds kDitherGpu{5e-4, 5e-4, 0.12}, kPaintGpu{5e-4, 5e-4, 0.05};

// codeStats of web/shaders/lab/gpu-kit.mjs over the RGB channels of RGBA8 words.
struct Codes { long n{0}, over2{0}; double mean{0}; int max{0}; };
Codes codes(const std::vector<std::uint8_t>& want, const std::vector<std::uint32_t>& got) {
    Codes c; double sum = 0;
    for (std::size_t i = 0; i < got.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const int d = std::abs(int(want[i * 4 + std::size_t(k)]) - int((got[i] >> (8 * k)) & 255u));
            sum += d; c.n++; c.max = std::max(c.max, d); if (d > 2) c.over2++;
        }
    c.mean = c.n ? sum / double(c.n) : 0;
    return c;
}
bool gpuWithin(const StyleCase& s, const GpuBounds& b) {
    return s.decisions > 0 && s.gpuMismatchFraction <= b.mismatch && s.gpuOver2Fraction <= b.over2 && s.gpuMean <= b.mean;
}

style::DitherGBuffer ditherGbuf(const fx::Bundle& b, const std::string& p, int w, int h) {
    style::DitherGBuffer g;
    g.w = w; g.h = h;
    g.albedo = b.arr<float>(p + "albedo", "f32"); g.kind = b.arr<std::uint8_t>(p + "kind", "u8"); g.depth = b.arr<float>(p + "depth", "f32");
    g.wpos = b.arr<float>(p + "wpos", "f32"); g.nrm = b.arr<float>(p + "nrm", "f32"); g.apos = b.arr<float>(p + "apos", "f32");
    g.anrm = b.arr<float>(p + "anrm", "f32"); g.vis = b.arr<float>(p + "vis", "f32"); g.haze = b.arr<float>(p + "haze", "f32");
    return g;
}

void ditherCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "ditherlight"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("width")), h = int(c.get("height")->number("height"));
        const std::string p = s.id + ".";
        const style::DitherGBuffer g = ditherGbuf(b, p, w, h);
        std::vector<style::DitherLight> lights;
        for (const Json& L : c.get("lights")->a) lights.push_back(fx::light(L));
        const style::DitherPlan plan = fx::planFromPreset(c.get("preset")->s, *c.get("overrides"), h, c.get("fov")->number("fov"));
        s.planEqual = fx::samePlan(plan, fx::plan(*c.get("plan")));
        const auto jsTone = b.arr<double>(p + "js_tone", "f64");
        const auto jsLevel = b.arr<std::uint8_t>(p + "js_level", "u8");
        const auto jsRgba = b.arr<std::uint8_t>(p + "js_rgba", "u8");
        const style::DitherFrame f = style::runDitherlight(plan, lights, g);
        s.decisions = long(f.level.size());
        for (std::size_t i = 0; i < f.level.size(); ++i) {
            s.cpuMaxAbs = std::max(s.cpuMaxAbs, std::abs(f.tone[i] - jsTone[i]));
            if (f.level[i] != jsLevel[i]) { (std::abs(f.fraction[i] - f.threshold[i]) <= kTie ? s.cpuExcused : s.cpuMismatch)++; continue; }
            for (std::size_t k = 0; k < 4; ++k) if (f.rgba[i * 4 + k] != jsRgba[i * 4 + k]) { s.cpuRgbaMismatch++; break; }
        }
        // Control: the hemisphere constant 0.5 changed to 0.6 must break the tone bound.
        style::DitherPlan bent = plan; bent.hemiConst = 0.6;
        const style::DitherFrame fc = style::runDitherlight(bent, lights, g);
        double worst = 0;
        for (std::size_t i = 0; i < fc.tone.size(); ++i) worst = std::max(worst, std::abs(fc.tone[i] - jsTone[i]));
        s.cpuControlFails = worst > 1e-9;
        s.cpuPass = s.planEqual && s.decisions == long(w) * h && s.cpuMaxAbs <= 1e-9 && s.cpuMismatch == 0 && s.cpuExcused <= kMaxExcused &&
                    s.cpuRgbaMismatch == 0 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> lv, packed;
            if (!style_gpu::runDither(*dev, plan, lights, g, lv, packed, r.error)) { r.cases.push_back(s); return; }
            s.gpuRun = true;
            for (std::size_t i = 0; i < lv.size(); ++i) if (lv[i] != f.level[i]) s.gpuMismatch++;
            const Codes st = codes(f.rgba, packed);
            s.gpuMismatchFraction = double(s.gpuMismatch) / double(s.decisions);
            s.gpuOver2Fraction = double(st.over2) / double(st.n); s.gpuMean = st.mean; s.gpuMax = st.max;
            // Control: the GPU levels against the reference with the threshold matrix one cell over.
            const style::DitherFrame shifted = style::runDitherlight(plan, lights, g, 1);
            long m = 0;
            for (std::size_t i = 0; i < lv.size(); ++i) if (lv[i] != shifted.level[i]) m++;
            s.gpuControlFails = double(m) / double(s.decisions) > kDitherGpu.mismatch;
            s.gpuPass = gpuWithin(s, kDitherGpu) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
}

style::Projector projector(const fx::Bundle& b, const Json& j, int k) {
    style::Projector pr;
    pr.cam = fx::camera(*j.get("cam"));
    pr.w = int(j.get("w")->number("w")); pr.h = int(j.get("h")->number("h"));
    pr.rgba = b.arr<std::uint8_t>("proj" + std::to_string(k) + ".rgba", "u8");
    pr.depth = b.arr<float>("proj" + std::to_string(k) + ".depth", "f32");
    return pr;
}
// The reference over a view: covered flag, hit count, 8-bit colour (0 where uncovered), margins.
struct PaintFrame { std::vector<std::uint8_t> cov, hits, rgba; std::vector<double> rgb, margin; };
PaintFrame paintFrame(const std::vector<const style::Projector*>& set, const std::vector<double>& reach, const style_gpu::PaintView& g,
                      const style::PaintCamera& view, const style::PaintOptions& o) {
    const std::size_t n = std::size_t(g.w) * g.h;
    PaintFrame f{std::vector<std::uint8_t>(n), std::vector<std::uint8_t>(n), std::vector<std::uint8_t>(n * 4), std::vector<double>(n * 3), std::vector<double>(n, 1e30)};
    for (std::size_t i = 0; i < n; ++i) {
        if (!g.hit[i]) continue;
        const style::V3 P{g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]}, N{g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]};
        const style::PaintShade s = style::shadePoint(set, reach, P, N, view, o);
        f.margin[i] = s.margin; f.hits[i] = std::uint8_t(s.hit);
        if (!s.covered) continue;
        f.cov[i] = 1;
        for (std::size_t k = 0; k < 3; ++k) {
            f.rgb[i * 3 + k] = s.rgb[k];
            f.rgba[i * 4 + k] = std::uint8_t(std::floor(std::min(1.0, std::max(0.0, s.rgb[k])) * 255 + 0.5));
        }
    }
    return f;
}
style::PaintCamera shiftedRight(style::PaintCamera c, int h) {
    const double step = style::pixelWorld(c, h, 60);   // one pixel at the perspective views' distance
    for (int k = 0; k < 3; ++k) c.pos[std::size_t(k)] += c.right[std::size_t(k)] * step;
    return c;
}
// Addendum A of evidence/cpp-port-style-bounds.json: every surface position moved one view pixel
// along the view's right axis, so each pixel reads its projectors one pixel off.
style_gpu::PaintView shiftedSurface(style_gpu::PaintView g, const style::PaintCamera& view) {
    for (std::size_t i = 0; i < g.hit.size(); ++i) {
        if (!g.hit[i]) continue;
        style::V3 P{g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]};
        const double depth = (P[0] - view.pos[0]) * view.fwd[0] + (P[1] - view.pos[1]) * view.fwd[1] + (P[2] - view.pos[2]) * view.fwd[2];
        const double step = style::pixelWorld(view, g.h, depth);
        for (std::size_t k = 0; k < 3; ++k) g.P[i * 3 + k] = float(P[k] + view.right[k] * step);
    }
    return g;
}
// The worst colour difference and the coverage flips of a frame against the JavaScript outputs.
std::pair<double, long> againstJs(const PaintFrame& f, const std::vector<std::uint8_t>& hit, const std::vector<std::uint8_t>& jsCov, const std::vector<double>& jsRgb) {
    double worst = 0; long covDiff = 0;
    for (std::size_t i = 0; i < f.cov.size(); ++i) {
        if (!hit[i]) continue;
        if (f.cov[i] != jsCov[i]) { covDiff++; continue; }
        if (f.cov[i]) for (std::size_t q = 0; q < 3; ++q) worst = std::max(worst, std::abs(f.rgb[i * 3 + q] - jsRgb[i * 3 + q]));
    }
    return {worst, covDiff};
}

void paintCases(rhi::Device* dev, const fx::Bundle& b, StyleParity& r) {
    std::vector<style::Projector> all;
    int k = 0;
    for (const Json& j : b.head.get("projectors")->a) all.push_back(projector(b, j, k++));
    const style::PaintOptions o = fx::options(*b.head.get("opt"));
    for (const Json& c : b.head.get("cases")->a) {
        StyleCase s; s.pass = "paintover"; s.id = c.get("id")->s;
        style_gpu::PaintView g;
        g.w = int(c.get("width")->number("width")); g.h = int(c.get("height")->number("height"));
        const std::string p = s.id + ".";
        g.P = b.arr<float>(p + "P", "f32"); g.N = b.arr<float>(p + "N", "f32"); g.hit = b.arr<std::uint8_t>(p + "hit", "u8");
        std::vector<const style::Projector*> set;
        for (const Json& u : c.get("use")->a) set.push_back(&all.at(std::size_t(u.number("use"))));
        const std::vector<double> reach = style::reaches(set, o);
        const style::PaintCamera view = fx::camera(*c.get("view"));
        const auto jsRgb = b.arr<double>(p + "js_rgb", "f64");
        const auto jsCov = b.arr<std::uint8_t>(p + "js_covered", "u8"), jsHits = b.arr<std::uint8_t>(p + "js_hits", "u8");
        const PaintFrame f = paintFrame(set, reach, g, view, o);
        for (std::size_t i = 0; i < f.cov.size(); ++i) {
            if (!g.hit[i]) continue;
            s.surface++; s.decisions++; s.covered += f.cov[i];
            if (f.cov[i] != jsCov[i]) { (f.margin[i] <= kTie ? s.cpuExcused : s.cpuMismatch)++; continue; }
            if (f.hits[i] != jsHits[i]) s.cpuRgbaMismatch++;
            if (f.cov[i]) for (std::size_t q = 0; q < 3; ++q) s.cpuMaxAbs = std::max(s.cpuMaxAbs, std::abs(f.rgb[i * 3 + q] - jsRgb[i * 3 + q]));
        }
        // The control as first written (the view camera one pixel to the right) is still run and
        // reported: an orthographic view's shading reads only the view direction, so it cannot fail
        // there. Addendum A's control moves the surface one view pixel instead.
        const auto [vWorst, vCov] = againstJs(paintFrame(set, reach, g, shiftedRight(view, g.h), o), g.hit, jsCov, jsRgb);
        s.viewShiftControlFails = vWorst > 1e-9 || vCov > kMaxExcused;
        const style_gpu::PaintView gs = shiftedSurface(g, view);
        const PaintFrame fc = paintFrame(set, reach, gs, view, o);
        const auto [worst, covDiff] = againstJs(fc, g.hit, jsCov, jsRgb);
        s.cpuControlFails = worst > 1e-9 || covDiff > kMaxExcused;
        s.cpuPass = s.surface > 0 && 2 * s.covered >= s.surface && s.cpuMaxAbs <= 1e-9 && s.cpuMismatch == 0 && s.cpuExcused <= kMaxExcused &&
                    s.cpuRgbaMismatch == 0 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!style_gpu::runPaint(*dev, set, reach, view, o, g, got, r.error)) { r.cases.push_back(s); return; }
            s.gpuRun = true;
            // As web/test/shaders-paintover.html: coverage decisions on surface pixels; colour where both cover.
            auto compare = [&](const PaintFrame& want, long& mism, Codes& st) {
                std::vector<std::uint8_t> a(got.size() * 4, 0); std::vector<std::uint32_t> bgot(got.size(), 0);
                mism = 0;
                for (std::size_t i = 0; i < got.size(); ++i) {
                    if (!g.hit[i]) continue;
                    const bool gc = (got[i] >> 24) == 255u, cc = want.cov[i] == 1;
                    if (gc != cc) { mism++; continue; }
                    if (!cc) continue;
                    for (std::size_t q = 0; q < 3; ++q) a[i * 4 + q] = want.rgba[i * 4 + q];
                    bgot[i] = got[i] & 0x00FFFFFFu;
                }
                st = codes(a, bgot);
            };
            Codes st; compare(f, s.gpuMismatch, st);
            s.gpuMismatchFraction = double(s.gpuMismatch) / double(s.decisions);
            s.gpuOver2Fraction = double(st.over2) / double(st.n); s.gpuMean = st.mean; s.gpuMax = st.max;
            long cm = 0; Codes cs; compare(fc, cm, cs);
            s.gpuControlFails = cs.mean > kPaintGpu.mean || double(cm) / double(s.decisions) > kPaintGpu.mismatch;
            s.gpuPass = gpuWithin(s, kPaintGpu) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
}
}  // namespace

LookKeysResult lookKeysParity(const std::string& path, std::string& err);   // style_lookkeys_parity.cpp
PostStackResult postStackParity(rhi::Device* dev, const std::string& path, std::string& err);   // post_stack_parity.cpp
int imageParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                      // style_parity_image.cpp
int quantiseParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                   // style_parity_quantise.cpp
int ditherParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                     // style_parity_dither.cpp
int adjacencyParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                  // style_parity_adjacency.cpp
int crtClassicParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                 // style_parity_crt.cpp
int vhsParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                        // style_parity_vhs.cpp
int fibreParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                      // style_parity_fibre.cpp
int filmParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                       // style_parity_film.cpp
int lensParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                       // style_parity_lens.cpp
int sagParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                        // style_parity_sag.cpp
int caricatureParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                 // style_parity_sag.cpp
int pixelParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                      // style_parity_pixel.cpp
int paintParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                      // style_parity_paint.cpp
int crtParity(rhi::Device* dev, const std::string& dir, StyleParity& r);                        // style_parity_crtp.cpp
int rtvisParity(const std::string& dir, StyleParity& r);                                        // style_parity_rtvis.cpp (CPU link only)
// The workgroup group (bounds-8): sag and caricature.
static int workgroupParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    int n = sagParity(dev, dir, r);
    if (r.error.empty()) n += caricatureParity(dev, dir, r);
    return n;
}
// The signal group (bounds-5): crt-classic, VHS and fibre.
static int signalParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    int n = crtClassicParity(dev, dir, r);
    if (r.error.empty()) n += vhsParity(dev, dir, r);
    if (r.error.empty()) n += fibreParity(dev, dir, r);
    return n;
}
std::string styleParityJson(const StyleParity& r);                          // style_parity_json.cpp

bool StyleParity::cpuPass() const {
    if (!only.empty()) {   // a development run of one group: its own cases only
        if (only == "poststack") return error.empty() && poststack.cpuPass();
        bool ok = error.empty() && !cases.empty();
        for (const StyleCase& c : cases) ok = ok && c.cpuPass;
        return ok;
    }
    bool ok = error.empty() && !cases.empty() && lookkeys.pass() && poststack.cpuPass();
    int dither = 0, paint = 0, image = 0, quant = 0, pdither = 0, adj = 0, sig = 0, film = 0, lens = 0, wg = 0, rtvis = 0, pix = 0, painterly = 0, tube = 0;
    for (const StyleCase& c : cases) {
        ok = ok && c.cpuPass; dither += c.pass == "ditherlight"; paint += c.pass == "paintover";
        image += c.pass == "purity" || c.pass == "lightpaint" || c.pass == "glaze" || c.pass == "hatch";
        quant += c.pass == "hysteresis" || c.pass == "palettelight";
        pdither += c.pass == "dither";
        adj += c.pass == "adjacency";
        sig += c.pass == "crtclassic" || c.pass == "vhs" || c.pass == "fibre";
        film += c.pass == "film";
        lens += c.pass == "lens";
        wg += c.pass == "sag" || c.pass == "caricature";
        rtvis += c.pass == "rtvis";
        pix += c.pass == "pixel";
        painterly += c.pass == "paint";
        tube += c.pass == "crt";
    }
    return ok && dither == expectDither && dither >= 5 && paint == expectPaint && paint >= 4 && image == expectImage && image >= 15 && quant == expectQuant && quant >= 8 && pdither == expectPaletteDither && pdither >= 114 &&
           adj == expectAdjacency && adj >= 4 && sig == expectSignal && sig >= 13 && film == expectFilm && film >= 6 && lens == expectLens && lens >= 4 && wg == expectWorkgroup && wg >= 7 && rtvis == expectRtvis && rtvis >= 12 && pix == expectPixel && pix >= 5 && painterly == expectPainterly && painterly >= 15 && tube == expectCrt && tube >= 11;
}
bool StyleParity::gpuPass() const {
    if (only == "poststack") return error.empty() && poststack.gpuPass();
    bool ok = error.empty() && !cases.empty() && (!only.empty() || poststack.gpuPass());
    for (const StyleCase& c : cases) ok = ok && (c.pass == "rtvis" || (c.gpuRun && c.gpuPass));   // rtvis is CPU code (rule 6)
    return ok;
}
std::string StyleParity::json() const { return styleParityJson(*this); }

// Development runs only (--only): the bundles of one group, judged on their own cases.
static StyleParity onlyGroup(rhi::Device* dev, const std::string& dir, const std::string& only) {
    StyleParity r;
    r.only = only;
    r.gpuWanted = dev != nullptr;
    if (dev) { r.backend = dev->backendName(); r.adapter = dev->adapter().description; }
    try {
        if (only == "poststack") r.poststack = postStackParity(dev, dir + "/poststack.rnfx", r.error);
        else if (only == "image") r.expectImage = imageParity(dev, dir, r);
        else if (only == "quantise") r.expectQuant = quantiseParity(dev, dir, r);
        else if (only == "dither") r.expectPaletteDither = ditherParity(dev, dir, r);
        else if (only == "adjacency") r.expectAdjacency = adjacencyParity(dev, dir, r);
        else if (only == "signal") r.expectSignal = signalParity(dev, dir, r);
        else if (only == "film") r.expectFilm = filmParity(dev, dir, r);
        else if (only == "lens") r.expectLens = lensParity(dev, dir, r);
        else if (only == "workgroup") r.expectWorkgroup = workgroupParity(dev, dir, r);
        else if (only == "rtvis") r.expectRtvis = rtvisParity(dir, r);
        else if (only == "pixel") r.expectPixel = pixelParity(dev, dir, r);
        else if (only == "paint") r.expectPainterly = paintParity(dev, dir, r);
        else if (only == "crt") r.expectCrt = crtParity(dev, dir, r);
        else r.error = "style-parity --only takes poststack, image, quantise, dither, adjacency, signal, film, lens, workgroup, rtvis, pixel, paint or crt";
    } catch (const std::exception& e) {
        r.error = std::string("fixture: ") + e.what();
    }
    return r;
}

StyleParity styleParity(rhi::Device* dev, const std::string& dir, const std::string& only) {
    if (!only.empty()) return onlyGroup(dev, dir, only);
    StyleParity r;
    r.gpuWanted = dev != nullptr;
    if (dev) { r.backend = dev->backendName(); r.adapter = dev->adapter().description; }
    try {
        fx::Bundle d, p;
        if (!fx::loadBundle(dir + "/ditherlight.rnfx", d, r.error) || !fx::loadBundle(dir + "/paintover.rnfx", p, r.error)) return r;
        r.expectDither = int(d.head.get("cases")->a.size()); r.expectPaint = int(p.head.get("cases")->a.size());
        ditherCases(dev, d, r);
        if (r.error.empty()) paintCases(dev, p, r);
        if (r.error.empty()) r.lookkeys = lookKeysParity(dir + "/lookkeys.json", r.error);
        if (r.error.empty()) r.poststack = postStackParity(dev, dir + "/poststack.rnfx", r.error);
        if (r.error.empty()) r.expectImage = imageParity(dev, dir, r);
        if (r.error.empty()) r.expectQuant = quantiseParity(dev, dir, r);
        if (r.error.empty()) r.expectPaletteDither = ditherParity(dev, dir, r);
        if (r.error.empty()) r.expectAdjacency = adjacencyParity(dev, dir, r);
        if (r.error.empty()) r.expectSignal = signalParity(dev, dir, r);
        if (r.error.empty()) r.expectFilm = filmParity(dev, dir, r);
        if (r.error.empty()) r.expectLens = lensParity(dev, dir, r);
        if (r.error.empty()) r.expectWorkgroup = workgroupParity(dev, dir, r);
        if (r.error.empty()) r.expectRtvis = rtvisParity(dir, r);
        if (r.error.empty()) r.expectPixel = pixelParity(dev, dir, r);
        if (r.error.empty()) r.expectPainterly = paintParity(dev, dir, r);
        if (r.error.empty()) r.expectCrt = crtParity(dev, dir, r);
    } catch (const std::exception& e) {
        r.error = std::string("fixture: ") + e.what();
    }
    return r;
}

}  // namespace raw::gpu_check
