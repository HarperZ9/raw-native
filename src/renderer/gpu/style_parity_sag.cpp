// Sag's and caricature's checks and GPU runners (passes with workgroup memory, one submission
// each): bounds and controls of evidence/cpp-port-style-bounds-8.json, written before the first run.
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_parity.hpp"
#include "raw/renderer/style_sag.hpp"
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

struct SagParamsGpu { std::uint32_t width, height, blank, settle, radius, pad0, pad1, pad2; float a[4], b[4]; };
static_assert(sizeof(SagParamsGpu) == 64, "SagGpuParams layout");
struct CariParamsGpu { std::uint32_t width, height, radius, steps, dir, pad0, pad1, pad2; float a[4], w[3][4]; };
static_assert(sizeof(CariParamsGpu) == 96, "CariGpuParams layout");

bool runSagGpu(rhi::Device& dev, const style::SagPlan& plan, const std::vector<float>& src, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const style::SagParams& p = plan.p;
    const SagParamsGpu P{std::uint32_t(plan.w), std::uint32_t(plan.h), std::uint32_t(plan.blank), p.settle ? 1u : 0u, std::uint32_t(plan.R), 0, 0, 0,
                         {float(p.S), float(plan.alpha), float(p.focus), float(p.sigma0)}, {float(p.bright), float(p.gamma), float(p.initial), 0}};
    const std::uint32_t n = std::uint32_t(plan.w * plan.h);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, BufferUsage::Uniform | BufferUsage::CopyDst, "sag params"), sb = gpu.make(src.size() * 4, st, "sag source");
    const rhi::BufferHandle ib = gpu.make(std::uint64_t(plan.h) * 4, st, "sag currents"), lb = gpu.make(std::uint64_t(plan.h) * 16, st, "sag lines");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "sag packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "sag readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), S = g.importBuffer("src", sb), I = g.importBuffer("I", ib), L = g.importBuffer("L", lb);
    const graph::Resource O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("sag upload", {{U, Access::CopyDst}, {S, Access::CopyDst}}, [&](graph::PassContext& c) { c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(sb, 0, src.data(), src.size() * 4); });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.addGroups("style_sag_current", {U, S, I}, std::uint32_t(plan.h), 1, 1, err) || !ps.addGroups("style_sag_scan", {U, I, L}, 1, 1, 1, err) ||
        !ps.add("style_sag_resample", {U, S, L, O}, n, err)) return false;
    g.addPass("sag readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

bool runCariGpu(rhi::Device& dev, const style::CariPlan& plan, const std::vector<float>& src, const std::vector<std::int32_t>& mat, double gain, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const style::CariParams& p = plan.p;
    CariParamsGpu P{std::uint32_t(plan.w), std::uint32_t(plan.h), std::uint32_t(plan.R), std::uint32_t(p.steps > 0 ? p.steps : 1), 0, 0, 0, 0,
                    {float(p.lambda), float(p.gain), float(gain), 0}, {}};
    for (int m = 0; m < 10; ++m) P.w[m / 4][m % 4] = float(p.weights[std::size_t(m)]);
    CariParamsGpu PV = P;
    PV.dir = 1;
    std::vector<float> taps;
    for (double t : plan.taps) taps.push_back(float(t));
    const std::uint32_t n = std::uint32_t(plan.w * plan.h);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, un = BufferUsage::Uniform | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, un, "cari params"), uv = gpu.make(sizeof P, un, "cari params v"), tb = gpu.make(taps.size() * 4, st, "cari taps");
    const rhi::BufferHandle mb = gpu.make(std::uint64_t(n) * 4, st, "cari materials"), sb = gpu.make(src.size() * 4, st, "cari source");
    const rhi::BufferHandle s1 = gpu.make(std::uint64_t(n) * 4, st, "cari salience"), s2 = gpu.make(std::uint64_t(n) * 4, st, "cari blur tmp"), s3 = gpu.make(std::uint64_t(n) * 4, st, "cari smoothed");
    const rhi::BufferHandle gb = gpu.make(std::uint64_t(n) * 8, st, "cari gradient"), lb = gpu.make(16, st, "cari lambda");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "cari packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "cari readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), UV = g.importBuffer("params v", uv), T = g.importBuffer("taps", tb), M = g.importBuffer("mat", mb), S = g.importBuffer("src", sb);
    const graph::Resource A = g.importBuffer("s", s1), B = g.importBuffer("t", s2), C = g.importBuffer("S", s3), G = g.importBuffer("g", gb), L = g.importBuffer("L", lb);
    const graph::Resource O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("cari upload", {{U, Access::CopyDst}, {UV, Access::CopyDst}, {T, Access::CopyDst}, {M, Access::CopyDst}, {S, Access::CopyDst}}, [&](graph::PassContext& c) {
        c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(uv, 0, &PV, sizeof P); c.cmd->upload(tb, 0, taps.data(), taps.size() * 4);
        c.cmd->upload(mb, 0, mat.data(), mat.size() * 4); c.cmd->upload(sb, 0, src.data(), src.size() * 4);
    });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_cari_sal", {U, M, A}, n, err) || !ps.add("style_cari_blur", {U, T, A, B}, n, err) || !ps.add("style_cari_blur", {UV, T, B, C}, n, err) ||
        !ps.add("style_cari_grad", {U, C, G}, n, err) || !ps.addGroups("style_cari_lambda", {U, G, L}, 1, 1, 1, err) || !ps.add("style_cari_warp", {U, S, G, L, O}, n, err)) return false;
    g.addPass("cari readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

struct Stats { int max{0}, p999{0}; double mean{0}; };
Stats stats(const std::vector<std::uint8_t>& want, const std::vector<std::uint32_t>& got) {
    long hist[256] = {}, n = 0; double sum = 0; Stats s;
    for (std::size_t i = 0; i < got.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) { const int d = std::abs(int(want[i * 4 + k]) - int((got[i] >> (8 * k)) & 255u)); hist[d]++; sum += d; n++; s.max = std::max(s.max, d); }
    s.mean = sum / double(n);
    long acc = 0;
    for (int d = 0; d < 256; ++d) { acc += hist[d]; if (double(acc) >= double(n) * 0.999) { s.p999 = d; break; } }
    return s;
}
bool withinGpu(const Stats& s) { return s.max <= 2 && s.p999 <= 1 && s.mean <= 0.05; }
bool rel(double a, double b, double tol) { return std::abs(a - b) <= tol * std::max(std::abs(b), 1e-300); }
bool vecRel(const std::vector<double>& a, const std::vector<double>& b, double tol) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > tol * std::max(1.0, std::abs(b[i]))) return false;
    return true;
}
double edgeOf(double codesValue) { const double q = codesValue + 0.5, fr = q - std::floor(q); return std::min(fr, 1 - fr); }
}  // namespace

// The sag cases; returns the number the bundle holds.
int sagParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/sag.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "sag"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("w")), h = int(c.get("height")->number("h"));
        const style::SagPlan plan = style::resolveSag(style::sagPreset(c.get("preset")->s), w, h);
        const Json& q = *c.get("plan");
        s.presetEqual = plan.blank == int(q.get("blank")->number("b")) && plan.R == int(q.get("R")->number("R")) && rel(plan.tauLines, q.get("tauLines")->number("t"), 1e-15) &&
                        rel(plan.alpha, q.get("alpha")->number("a"), 1e-15);
        if (!s.presetEqual) s.presetDetail = "blank/R/tauLines/alpha";
        const auto src = b.arr<float>(s.id + ".src", "f32");
        const auto jsOut = b.arr<float>(s.id + ".out", "f32");
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        style::SagScan sc, bsc;
        const std::vector<float> out = style::runSag(plan, src, sc);
        style::SagPlan bentPlan = plan; bentPlan.p.sigma0 *= 1.5;
        const std::vector<float> bent = style::runSag(bentPlan, src, bsc);
        const char* names[5] = {"I", "v", "k", "Y", "sig"};
        const std::vector<double>* mine[5] = {&sc.I, &sc.v, &sc.k, &sc.Y, &sc.sig};
        std::string detail;
        for (int k = 0; k < 5; ++k) if (!vecRel(*mine[k], b.arr<double>(s.id + "." + names[k], "f64"), 1e-12)) detail += std::string(names[k]) + " ";
        if (!detail.empty()) { s.presetEqual = false; s.presetDetail += " scan: " + detail; }
        double mx = 0, bmx = 0;
        for (std::size_t i = 0; i < jsOut.size(); ++i) { if ((i & 3) == 3) continue; mx = std::max(mx, std::abs(double(out[i]) - jsOut[i])); bmx = std::max(bmx, std::abs(double(bent[i]) - jsOut[i])); }
        s.cpuMaxAbs = mx;
        std::vector<std::uint8_t> codes(out.size()), bcodes(out.size());
        for (std::size_t i = 0; i < out.size(); ++i) { codes[i] = (i & 3) == 3 ? 255 : style::sagSignal8(out[i]); bcodes[i] = (i & 3) == 3 ? 255 : style::sagSignal8(bent[i]); }
        s.decisions = long(js.size() / 4 * 3);
        for (std::size_t i = 0; i < js.size(); ++i) {
            if ((i & 3) == 3 || codes[i] == js[i]) continue;
            (edgeOf(std::min(1.0, std::max(0.0, double(jsOut[i]))) * 255) <= 1e-4 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = bmx > 1e-6;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-6 && s.cpuMismatch == 0 && s.cpuExcused <= 2 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runSagGpu(*dev, plan, src, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const Stats st = stats(codes, got), scs = stats(bcodes, got);
            s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean;
            s.gpuControlFails = !withinGpu(scs);
            s.gpuPass = withinGpu(st) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

// The caricature cases; returns the number the bundle holds.
int caricatureParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/caricature.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "caricature"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("w")), h = int(c.get("height")->number("h"));
        const double gain = c.get("gain")->number("gain");
        const style::CariPlan plan = style::resolveCaricature(style::cariPreset(c.get("preset")->s), w, h);
        const auto jsTaps = b.arr<double>(s.id + ".taps", "f64");
        s.presetEqual = plan.R == int(c.get("R")->number("R")) && vecRel(plan.taps, jsTaps, 1e-15);
        if (!s.presetEqual) s.presetDetail = "R/taps";
        const auto src = b.arr<float>(s.id + ".src", "f32");
        const auto mat32 = b.arr<std::uint32_t>(s.id + ".mat", "u32");
        std::vector<std::int32_t> mat(mat32.size());
        for (std::size_t i = 0; i < mat.size(); ++i) mat[i] = std::int32_t(mat32[i]);
        const auto jsOut = b.arr<float>(s.id + ".out", "f32");
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        style::CariFields F, BF;
        const std::vector<float> out = style::runCaricature(plan, src, mat, F);
        style::CariPlan bentPlan = plan; bentPlan.p.gain *= 1.25;
        const std::vector<float> bent = style::runCaricature(bentPlan, src, mat, BF);
        std::string detail;
        if (!vecRel(F.S, b.arr<double>(s.id + ".S", "f64"), 1e-12)) detail += "S ";
        if (!vecRel(F.g, b.arr<double>(s.id + ".g", "f64"), 1e-12)) detail += "g ";
        if (!rel(F.lambda, c.get("lambda")->number("l"), 1e-12)) detail += "lambda ";
        if (!detail.empty()) { s.presetEqual = false; s.presetDetail += " fields: " + detail; }
        double mx = 0, bmx = 0;
        for (std::size_t i = 0; i < jsOut.size(); ++i) {
            if ((i & 3) == 3) continue;
            mx = std::max(mx, std::abs(double(out[i]) - jsOut[i]) / std::max(1.0, std::abs(double(jsOut[i]))));
            bmx = std::max(bmx, std::abs(double(bent[i]) - jsOut[i]) / std::max(1.0, std::abs(double(jsOut[i]))));
        }
        s.cpuMaxAbs = mx;
        std::vector<std::uint8_t> codes(out.size()), bcodes(out.size());
        for (std::size_t i = 0; i < out.size(); ++i) { codes[i] = (i & 3) == 3 ? 255 : style::encodeLab8(out[i], gain); bcodes[i] = (i & 3) == 3 ? 255 : style::encodeLab8(bent[i], gain); }
        s.decisions = long(js.size() / 4 * 3);
        for (std::size_t i = 0; i < js.size(); ++i) {
            if ((i & 3) == 3 || codes[i] == js[i]) continue;
            (style::edgeLab8(jsOut[i], gain) <= 1e-4 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = bmx > 1e-6;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-6 && s.cpuMismatch == 0 && s.cpuExcused <= 2 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runCariGpu(*dev, plan, src, mat, gain, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const Stats st = stats(codes, got), scs = stats(bcodes, got);
            s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean;
            s.gpuControlFails = !withinGpu(scs);
            s.gpuPass = withinGpu(st) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
