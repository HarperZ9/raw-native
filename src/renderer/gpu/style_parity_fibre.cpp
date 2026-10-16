// Fibre-network watercolour's checks and its GPU runner (field, tensor, init, steps and out, one
// submission): bounds and controls of evidence/cpp-port-style-bounds-5.json, fibre, written before
// the first run.
#include "raw/renderer/style_fibre.hpp"
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_parity.hpp"
#include "gpu_util.hpp"
#include "style_fixture.hpp"
#include "style_graph.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace raw::gpu_check {
namespace {
using assets::Json;
using rhi::Access;
using rhi::BufferUsage;
namespace fx = style_fx;

struct FibreParamsGpu { std::uint32_t width, height, perCell, pad0; float a[4], b[4], c[4], paper[4]; };
static_assert(sizeof(FibreParamsGpu) == 80, "FibreParams layout");

bool runFibreGpu(rhi::Device& dev, const style::FibrePlan& plan, const std::vector<float>& src, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const style::FibreParams& p = plan.p;
    const FibreParamsGpu P{std::uint32_t(plan.w), std::uint32_t(plan.h), std::uint32_t(p.perCell), 0,
                           {float(plan.C), float(plan.L), float(p.width), float(p.md)}, {float(p.spread), float(p.k0), float(p.k1), float(p.rate)},
                           {float(p.floc), float(p.evap), float(plan.dt), float(p.strength)}, {float(p.paper[0]), float(p.paper[1]), float(p.paper[2]), float(p.edgeDry)}};
    const std::uint32_t n = std::uint32_t(plan.w * plan.h);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, BufferUsage::Uniform | BufferUsage::CopyDst, "fibre params"), sb = gpu.make(std::uint64_t(n) * 16, st, "fibre source");
    const rhi::BufferHandle fb = gpu.make(std::uint64_t(n) * 16, st, "fibre field"), xb = gpu.make(std::uint64_t(n) * 4, st, "fibre crossings");
    const rhi::BufferHandle kb = gpu.make(std::uint64_t(n) * 16, st, "fibre tensor"), s0 = gpu.make(std::uint64_t(n) * 16, st, "fibre wash 0");
    const rhi::BufferHandle s1 = gpu.make(std::uint64_t(n) * 16, st, "fibre wash 1"), db = gpu.make(std::uint64_t(n) * 16, st, "fibre deposit");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "fibre packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "fibre readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), S = g.importBuffer("src", sb), F = g.importBuffer("F", fb), X = g.importBuffer("X", xb);
    const graph::Resource K = g.importBuffer("K", kb), W0 = g.importBuffer("S0", s0), W1 = g.importBuffer("S1", s1), D = g.importBuffer("D", db);
    const graph::Resource O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("fibre upload", {{U, Access::CopyDst}, {S, Access::CopyDst}}, [&](graph::PassContext& c) {
        c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(sb, 0, src.data(), src.size() * 4);
    });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_fibre_field", {U, F, X}, n, err) || !ps.add("style_fibre_tensor", {U, F, K}, n, err) || !ps.add("style_fibre_init", {U, S, W0, D}, n, err)) return false;
    graph::Resource a = W0, b = W1;
    for (int it = 0; it < p.steps; ++it) {
        if (!ps.add("style_fibre_step", {U, K, X, a, b, D}, n, err)) return false;
        std::swap(a, b);
    }
    if (!ps.add("style_fibre_out", {U, a, D, O}, n, err)) return false;
    g.addPass("fibre readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
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
std::vector<std::uint8_t> codesOf(const std::vector<double>& v) {
    std::vector<std::uint8_t> o(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::encodeDisplay8(v[i]);
    return o;
}
double maxRel(const std::vector<double>& a, const std::vector<double>& b) {
    double m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if ((i & 3) != 3) m = std::max(m, std::abs(a[i] - b[i]) / std::max(1.0, std::abs(b[i])));
    return m;
}
bool withinGpu(const Stats& s) { return s.max <= 3 && s.p999 <= 1 && s.mean <= 0.05; }
}  // namespace

// The fibre cases; returns the number the bundle holds.
int fibreParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/fibre.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "fibre"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("width")), h = int(c.get("height")->number("height"));
        const style::FibrePlan plan = style::resolveFibre(style::fibrePreset(c.get("preset")->s), w, h);
        const Json& q = *c.get("plan");
        s.presetEqual = plan.L == q.get("L")->number("L") && plan.C == q.get("C")->number("C") && plan.dt == q.get("dt")->number("dt");
        if (!s.presetEqual) s.presetDetail = "L/C/dt";
        const auto src = b.arr<float>(s.id + ".src", "f32");
        const auto js = b.arr<double>(s.id + ".js", "f64");
        style::FibrePlan bentPlan = plan; bentPlan.p.strength *= 1.1;
        const std::vector<double> cpp = style::runFibre(plan, src), bent = style::runFibre(bentPlan, src);
        s.decisions = long(js.size() / 4);
        s.cpuMaxAbs = maxRel(cpp, js);
        for (std::size_t i = 0; i < js.size(); ++i) {
            if ((i & 3) == 3 || style::encodeDisplay8(cpp[i]) == style::encodeDisplay8(js[i])) continue;
            (style::edgeDisplay8(cpp[i]) <= 1e-9 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = maxRel(bent, js) > 1e-9;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-9 && s.cpuMismatch == 0 && s.cpuExcused <= 2 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runFibreGpu(*dev, plan, src, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const Stats st = stats(codesOf(cpp), got), sc = stats(codesOf(bent), got);
            s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean;
            s.gpuControlFails = !withinGpu(sc);
            s.gpuPass = withinGpu(st) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
