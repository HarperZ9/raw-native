// crt-classic's checks and its GPU runner (phosphor, down, 3 x 2 box passes a glow field, add and
// tube, one submission): bounds and controls of evidence/cpp-port-style-bounds-5.json, crt_classic,
// written before the first run.
#include "raw/renderer/style_crt_classic.hpp"
#include "raw/renderer/style_parity.hpp"
#include "gpu_util.hpp"
#include "style_fixture.hpp"
#include "style_graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
using assets::Json;
using rhi::Access;
using rhi::BufferUsage;
namespace fx = style_fx;

struct ClassicParamsGpu { std::uint32_t width, height, cell, phosphor, flatCell, flatEven, flatOdd, q, lw, lh, dir, radius; float a[4], b[4]; };
static_assert(sizeof(ClassicParamsGpu) == 80, "ClassicParams layout");

style::ClassicOptions optionsOf(const Json& o) {
    style::ClassicOptions c;
    auto num = [&](const char* k, double d) { const Json* v = o.get(k); return v ? v->number(k) : d; };
    c.cell = int(num("cell", 1));
    c.scanlines = o.get("scanlines") && o.get("scanlines")->b;
    c.scanStrength = num("scanStrength", 0); c.beam = num("beam", 0.5);
    c.mask = o.get("mask") ? o.get("mask")->s : "none";
    c.maskStrength = num("maskStrength", 0); c.bloom = num("bloom", 0); c.halation = num("halation", 0);
    c.curvature = num("curvature", 0); c.aberration = num("aberration", 0); c.vignette = num("vignette", 0);
    return c;
}
// The built-in control: every option above zero among the strengths and amounts, times 1.1.
style::ClassicOptions bent(style::ClassicOptions o) {
    for (double* v : {&o.scanStrength, &o.beam, &o.maskStrength, &o.bloom, &o.halation, &o.curvature, &o.aberration, &o.vignette})
        if (*v > 0) *v *= 1.1;
    return o;
}

bool runClassic(rhi::Device& dev, const style::ClassicOptions& o, int w, int h, const std::vector<std::uint8_t>& rgba, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const style::ClassicTables& T = style::classicTables();
    const int cell = std::max(1, o.cell), q = std::min(w, h) < 320 ? 2 : 4, lw = (w + q - 1) / q, lh = (h + q - 1) / q;
    const bool scan = o.scanlines, maskOn = o.mask != "none" && o.maskStrength > 0;
    const double s = std::min(1.0, std::max(0.0, o.scanStrength));
    ClassicParamsGpu P{std::uint32_t(w), std::uint32_t(h), std::uint32_t(cell), (scan || maskOn) ? 1u : 0u, (scan && cell < 2) ? 1u : 0u,
                       std::uint32_t(int((1 - s) * 256)), std::uint32_t(int(1.1 * 256)), std::uint32_t(q), std::uint32_t(lw), std::uint32_t(lh), 0, 0,
                       {float(std::max(0.0, o.bloom)), float(std::max(0.0, o.halation)), float(std::max(0.0, o.curvature)), float(std::max(0.0, o.aberration))},
                       {float(std::max(0.0, o.vignette)), 0, 0, 0}};
    ClassicParamsGpu V[4] = {P, P, P, P};
    const int rB = style::classicBoxRadius(8.0 / q), rH = style::classicBoxRadius(28.0 / q);
    V[0].dir = 0; V[0].radius = std::uint32_t(rB); V[1].dir = 1; V[1].radius = std::uint32_t(rB);
    V[2].dir = 0; V[2].radius = std::uint32_t(rH); V[3].dir = 1; V[3].radius = std::uint32_t(rH);
    const std::vector<std::int32_t> beam = style::classicBeamFixed(o), mt = style::classicMaskFixed(o);
    std::vector<std::uint32_t> srgb(T.toSrgb.begin(), T.toSrgb.end()), src(std::size_t(w) * h);
    std::memcpy(src.data(), rgba.data(), src.size() * 4);
    const std::uint32_t n = std::uint32_t(w * h), m = std::uint32_t(lw * lh);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, un = BufferUsage::Uniform | BufferUsage::CopyDst;
    rhi::BufferHandle ub[5];
    for (int k = 0; k < 5; ++k) ub[k] = gpu.make(sizeof P, un, "classic params");
    const rhi::BufferHandle bb = gpu.make(beam.size() * 4, st, "classic beam"), mb = gpu.make(mt.size() * 4, st, "classic mask");
    const rhi::BufferHandle lb = gpu.make(256 * 4, st, "classic toLin"), sb = gpu.make(4097 * 4, st, "classic toSrgb");
    const rhi::BufferHandle ib = gpu.make(std::uint64_t(n) * 4, st, "classic in"), ab = gpu.make(std::uint64_t(n) * 4, st, "classic a");
    const rhi::BufferHandle b2 = gpu.make(std::uint64_t(n) * 4, st, "classic b"), cb = gpu.make(std::uint64_t(n) * 4, st, "classic out");
    const rhi::BufferHandle linb = gpu.make(std::uint64_t(m) * 12, st, "classic lin"), brb = gpu.make(std::uint64_t(m) * 12, st, "classic bright");
    const rhi::BufferHandle tmb = gpu.make(std::uint64_t(m) * 12, st, "classic tmp");
    const rhi::BufferHandle rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "classic readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    graph::Resource U[5];
    for (int k = 0; k < 5; ++k) U[k] = g.importBuffer("params", ub[k]);
    const graph::Resource Bm = g.importBuffer("beam", bb), M = g.importBuffer("mask", mb), L = g.importBuffer("toLin", lb), S8 = g.importBuffer("toSrgb", sb);
    const graph::Resource I = g.importBuffer("in", ib), A = g.importBuffer("a", ab), B = g.importBuffer("b", b2), C = g.importBuffer("out", cb);
    const graph::Resource Lin = g.importBuffer("lin", linb), Br = g.importBuffer("bright", brb), Tm = g.importBuffer("tmp", tmb), RB = g.importBuffer("readback", rb);
    g.addPass("classic upload", {{U[0], Access::CopyDst}, {U[1], Access::CopyDst}, {U[2], Access::CopyDst}, {U[3], Access::CopyDst}, {U[4], Access::CopyDst},
                                 {Bm, Access::CopyDst}, {M, Access::CopyDst}, {L, Access::CopyDst}, {S8, Access::CopyDst}, {I, Access::CopyDst}},
              [&](graph::PassContext& c) {
                  c.cmd->upload(ub[0], 0, &P, sizeof P);
                  for (int k = 0; k < 4; ++k) c.cmd->upload(ub[k + 1], 0, &V[k], sizeof P);
                  c.cmd->upload(bb, 0, beam.data(), beam.size() * 4); c.cmd->upload(mb, 0, mt.data(), mt.size() * 4);
                  c.cmd->upload(lb, 0, T.toLin.data(), 256 * 4); c.cmd->upload(sb, 0, srgb.data(), srgb.size() * 4);
                  c.cmd->upload(ib, 0, src.data(), src.size() * 4);
              });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_crt_phosphor", {U[0], Bm, M, I, A}, n, err)) return false;
    const bool bloom = o.bloom > 0, hal = o.halation > 0;
    if (bloom || hal) {
        if (!ps.add("style_crt_down", {U[0], L, A, Lin, Br}, m, err)) return false;
        for (int k = 0; bloom && k < 3; ++k)
            if (!ps.add("style_crt_box", {U[1], Br, Tm}, m, err) || !ps.add("style_crt_box", {U[2], Tm, Br}, m, err)) return false;
        for (int k = 0; hal && k < 3; ++k)
            if (!ps.add("style_crt_box", {U[3], Lin, Tm}, m, err) || !ps.add("style_crt_box", {U[4], Tm, Lin}, m, err)) return false;
    }
    if (!ps.add("style_crt_add", {U[0], L, S8, Br, Lin, A, B}, n, err) || !ps.add("style_crt_tube", {U[0], B, C}, n, err)) return false;
    g.addPass("classic readback", {{C, Access::CopySrc}, {RB, Access::CopyDst}}, [cb, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(cb, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

int maxDiff(const std::vector<std::uint8_t>& want, const std::vector<std::uint32_t>& got, double* mean) {
    int mx = 0; double sum = 0;
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) { const int d = std::abs(int(want[i * 4 + k]) - int((got[i] >> (8 * k)) & 255u)); mx = std::max(mx, d); sum += d; }
    if (mean) *mean = sum / double(got.size() * 3);
    return mx;
}
template <class A, class B> bool sameInts(const A& a, const B& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (std::int64_t(a[i]) != std::int64_t(b[i])) return false;
    return true;
}
}  // namespace

// The crt-classic cases; returns the number the bundle holds.
int crtClassicParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/crtclassic.rnfx", b, r.error)) return 0;
    const style::ClassicTables& T = style::classicTables();
    const auto jsLin = b.arr<float>("toLin", "f32");
    const auto jsSrgb = b.arr<std::uint8_t>("toSrgb", "u8");
    bool linEq = jsLin.size() == T.toLin.size() && std::memcmp(jsLin.data(), T.toLin.data(), jsLin.size() * 4) == 0;
    const bool srgbEq = sameInts(jsSrgb, T.toSrgb);
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "crtclassic"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("width")), h = int(c.get("height")->number("height"));
        const style::ClassicOptions o = optionsOf(*c.get("opts")), ob = bent(o);
        const bool beamEq = sameInts(style::classicBeamFixed(o), b.arr<std::uint32_t>(s.id + ".beam", "u32"));
        const bool maskEq = sameInts(style::classicMaskFixed(o), b.arr<std::uint32_t>(s.id + ".mask", "u32"));
        s.presetEqual = linEq && srgbEq && beamEq && maskEq;
        if (!s.presetEqual) s.presetDetail = std::string(linEq ? "" : "toLin ") + (srgbEq ? "" : "toSrgb ") + (beamEq ? "" : "beam ") + (maskEq ? "" : "mask");
        const auto frame = b.arr<std::uint8_t>("frame." + c.get("frame")->s, "u8");
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        std::vector<std::uint8_t> cpp = frame, bt = frame;
        style::crtClassic(cpp, w, h, o);
        style::crtClassic(bt, w, h, ob);
        s.decisions = long(js.size());
        int mx = 0;
        for (std::size_t i = 0; i < js.size(); ++i) if (cpp[i] != js[i]) { s.cpuMismatch++; mx = std::max(mx, std::abs(int(cpp[i]) - int(js[i]))); }
        s.cpuMaxAbs = mx;
        long bentDiff = 0;
        for (std::size_t i = 0; i < js.size(); ++i) bentDiff += bt[i] != js[i];
        s.cpuControlFails = bentDiff > 0;
        s.cpuPass = s.presetEqual && s.cpuMismatch == 0 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runClassic(*dev, o, w, h, frame, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            s.gpuMax = maxDiff(cpp, got, &s.gpuMean);
            s.gpuControlFails = maxDiff(bt, got, nullptr) > 1;
            s.gpuPass = s.gpuMax <= 1 && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
