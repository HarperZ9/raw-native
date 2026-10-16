// Development adjacency's checks and its GPU runner (a frame graph of prep, steps x (develop,
// chemistry) and out passes in one submission): bounds and controls of
// evidence/cpp-port-style-bounds-4.json, adjacency, written before the first run.
#include "raw/graph/frame_graph.hpp"
#include "raw/renderer/style_adjacency.hpp"
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_parity.hpp"
#include "shaders/style_layout.hpp"
#include "style_fixture.hpp"
#include "style_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
using assets::Json;
using rhi::Access;
using rhi::BufferUsage;
namespace fx = style_fx;

struct AdjParamsGpu { std::uint32_t width, height, s, cw, ch, pad0, pad1, pad2; float a[4], b[4], c[4], d[4]; };
static_assert(sizeof(AdjParamsGpu) == 96, "AdjParams layout");
void put4(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }

bool runAdjacency(rhi::Device& dev, const style::AdjPlan& plan, const std::vector<float>& scene, double gain, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(plan.w * plan.h), m = std::uint32_t(plan.cw * plan.ch);
    const style::AdjParams& p = plan.p;
    AdjParamsGpu P{std::uint32_t(plan.w), std::uint32_t(plan.h), std::uint32_t(plan.s), std::uint32_t(plan.cw), std::uint32_t(plan.ch), 0, 0, 0, {}, {}, {}, {}};
    put4(P.a, plan.Dc, plan.Db, plan.dt, p.k); put4(P.b, p.eta, p.r, p.beta, p.etaB); put4(P.c, p.rB, p.gamma, p.H0, p.exposure); put4(P.d, plan.decay, gain, 0, 0);
    std::vector<float> init(std::size_t(m) * 4, 0.0f);
    for (std::size_t i = 0; i < m; ++i) init[i * 4] = 1;
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, BufferUsage::Uniform | BufferUsage::CopyDst, "adj params"), sb = gpu.make(std::uint64_t(n) * 16, st, "adj scene");
    const rhi::BufferHandle ab = gpu.make(std::uint64_t(n) * 16, st, "adj developable"), db = gpu.make(std::uint64_t(n) * 16, st, "adj density");
    const rhi::BufferHandle f0 = gpu.make(std::uint64_t(m) * 16, st, "adj chemistry 0"), f1 = gpu.make(std::uint64_t(m) * 16, st, "adj chemistry 1");
    const rhi::BufferHandle o8 = gpu.make(std::uint64_t(n) * 4, st, "adj packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "adj readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), S = g.importBuffer("scene", sb), A = g.importBuffer("A", ab), D = g.importBuffer("D", db);
    const graph::Resource F0 = g.importBuffer("F0", f0), F1 = g.importBuffer("F1", f1), O = g.importBuffer("out8", o8), RB = g.importBuffer("readback", rb);
    g.addPass("adj upload", {{U, Access::CopyDst}, {S, Access::CopyDst}, {F0, Access::CopyDst}}, [&](graph::PassContext& c) {
        c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(sb, 0, scene.data(), scene.size() * 4); c.cmd->upload(f0, 0, init.data(), init.size() * 4);
    });
    auto dispatch = [&](const char* name, std::vector<graph::Resource> binds, std::uint32_t count) {
        const int pi = style_gpu::passIndex(name);
        if (pi < 0) { err = std::string("no style pass ") + name; return false; }
        const auto& L = style_passes::kPasses[pi];
        const rhi::PipelineHandle pipe = util::compute(dev, L.name, L.binds, L.bindCount, err);
        if (!pipe.valid()) return false;
        std::vector<graph::Use> uses;
        for (int k = 0; k < L.bindCount; ++k)
            uses.push_back({binds[std::size_t(k)], L.binds[k] == rhi::Binding::Uniform ? Access::Uniform : L.binds[k] == rhi::Binding::StorageRead ? Access::StorageRead : Access::StorageWrite});
        g.addPass(name, uses, [pipe, binds, count](graph::PassContext& c) {
            std::vector<rhi::BufferHandle> h;
            for (graph::Resource r : binds) h.push_back(c.buffer(r));
            c.cmd->dispatch(pipe, h, (count + 63) / 64, 1, 1);
        });
        return true;
    };
    if (!dispatch("style_adj_prep", {U, S, A, D}, n)) return false;
    graph::Resource F = F0, G = F1;
    for (int it = 0; it < plan.steps; ++it) {
        if (!dispatch("style_adj_develop", {U, A, F, D}, n) || !dispatch("style_adj_chem", {U, D, F, G}, m)) return false;
        std::swap(F, G);
    }
    if (!dispatch("style_adj_out", {U, S, A, D, O}, n)) return false;
    g.addPass("adj readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [o8, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(o8, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    const void* q = dev.mapRead(rb, std::uint64_t(n) * 4, err);
    if (!q) return false;
    packed.resize(n);
    std::memcpy(packed.data(), q, std::size_t(n) * 4);
    dev.unmap(rb);
    return true;
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
    for (std::size_t i = 0; i < v.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::encodeLab8(v[i]);
    return o;
}
double maxRel(const std::vector<double>& a, const std::vector<double>& b) {
    double m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if ((i & 3) != 3) m = std::max(m, std::abs(a[i] - b[i]) / std::max(1.0, std::abs(b[i])));
    return m;
}
bool withinGpu(const Stats& s) { return s.max <= 3 && s.p999 <= 1 && s.mean <= 0.05; }
}  // namespace

// The adjacency cases; returns the number the bundle holds.
int adjacencyParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/adjacency.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "adjacency"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("width")), h = int(c.get("height")->number("height"));
        const style::AdjPlan plan = style::resolveAdjacency(style::adjPreset(c.get("preset")->s), w, h);
        const Json& q = *c.get("plan");
        auto eq = [&](const char* k, double v) { return v == q.get(k)->number(k); };
        s.presetEqual = eq("s", plan.s) && eq("cw", plan.cw) && eq("ch", plan.ch) && eq("steps", plan.steps) && std::abs(plan.Dc - q.get("Dc")->number("Dc")) <= 1e-12 * plan.Dc &&
                        std::abs(plan.Db - q.get("Db")->number("Db")) <= 1e-12 * plan.Db && eq("dt", plan.dt) && std::abs(plan.decay - q.get("decay")->number("decay")) <= 1e-15;
        const auto src = b.arr<float>(s.id + ".src", "f32");
        const auto js = b.arr<double>(s.id + ".js", "f64");
        style::AdjPlan bentPlan = plan; bentPlan.p.k *= 1.1;
        const std::vector<double> cpp = style::runAdjacency(plan, src), bent = style::runAdjacency(bentPlan, src);
        s.decisions = long(js.size() / 4);
        s.cpuMaxAbs = maxRel(cpp, js);
        for (std::size_t i = 0; i < js.size(); ++i) {
            if ((i & 3) == 3 || style::encodeLab8(cpp[i]) == style::encodeLab8(js[i])) continue;
            (style::edgeLab8(cpp[i]) <= 1e-9 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = maxRel(bent, js) > 1e-9;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-9 && s.cpuMismatch == 0 && s.cpuExcused <= 2 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runAdjacency(*dev, plan, src, 2.4, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
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
