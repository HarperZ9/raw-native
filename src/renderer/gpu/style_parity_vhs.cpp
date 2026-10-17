// VHS's checks and its GPU runner (sample, tape, play and show, one submission): bounds and
// controls of evidence/cpp-port-style-bounds-5.json, vhs, written before the first run.
#include "raw/renderer/style_parity.hpp"
#include "raw/renderer/style_vhs.hpp"
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

struct VhsParamsGpu {
    std::uint32_t n, lines, srcW, srcH, outW, outH, frame, seed, lh, ph, chh, pad0;
    float a[4], b[4], c[4], yiq[3][4], inv[3][4];
};
static_assert(sizeof(VhsParamsGpu) == 192, "VhsParams layout");

bool runVhs(rhi::Device& dev, const style::VhsPlan& plan, const std::vector<float>& src, int f, int ow, int oh, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const style::VhsParams& p = plan.p;
    const auto inv = style::vhsYiqInverse();
    VhsParamsGpu P{};
    P.n = std::uint32_t(plan.N); P.lines = std::uint32_t(plan.lines); P.srcW = std::uint32_t(plan.srcW); P.srcH = std::uint32_t(plan.srcH);
    P.outW = std::uint32_t(ow); P.outH = std::uint32_t(oh); P.frame = std::uint32_t(f); P.seed = std::uint32_t(p.seed);
    P.lh = std::uint32_t(plan.luma.size() / 2); P.ph = std::uint32_t(plan.peak.size() / 2); P.chh = std::uint32_t(plan.chroma.size() / 2);
    const float a[4] = {float(p.jitter), float(p.headSwitchLines), float(p.headSkew), float(p.tracking)};
    const float b[4] = {float(plan.lumaNoise), float(plan.chromaNoise), float(p.peaking), float(plan.delay)};
    const float c[4] = {float(plan.phaseNoise), float(p.dropouts * plan.g), 0, 0};
    std::memcpy(P.a, a, sizeof a); std::memcpy(P.b, b, sizeof b); std::memcpy(P.c, c, sizeof c);
    for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) { P.yiq[r][k] = float(style::kYiq[std::size_t(3 * r + k)]); P.inv[r][k] = float(inv[std::size_t(3 * r + k)]); }
    std::vector<float> taps;
    for (const auto* t : {&plan.luma, &plan.peak, &plan.chroma}) for (double v : *t) taps.push_back(float(v));
    const std::uint32_t n = std::uint32_t(plan.N * plan.lines), no = std::uint32_t(ow * oh);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, BufferUsage::Uniform | BufferUsage::CopyDst, "vhs params"), tb = gpu.make(taps.size() * 4, st, "vhs taps");
    const rhi::BufferHandle sb = gpu.make(src.size() * 4, st, "vhs source"), smb = gpu.make(std::uint64_t(n) * 16, st, "vhs sampled");
    const rhi::BufferHandle tpb = gpu.make(std::uint64_t(n) * 16, st, "vhs tape"), sgb = gpu.make(std::uint64_t(n) * 16, st, "vhs signal");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(no) * 4, st, "vhs display"), rb = gpu.make(std::uint64_t(no) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "vhs readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), T = g.importBuffer("taps", tb), S = g.importBuffer("src", sb), SM = g.importBuffer("sm", smb);
    const graph::Resource TP = g.importBuffer("tp", tpb), SG = g.importBuffer("sig", sgb), O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("vhs upload", {{U, Access::CopyDst}, {T, Access::CopyDst}, {S, Access::CopyDst}}, [&](graph::PassContext& x) {
        x.cmd->upload(ub, 0, &P, sizeof P); x.cmd->upload(tb, 0, taps.data(), taps.size() * 4); x.cmd->upload(sb, 0, src.data(), src.size() * 4);
    });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_vhs_sample", {U, S, SM}, n, err) || !ps.add("style_vhs_tape", {U, T, SM, TP}, n, err) ||
        !ps.add("style_vhs_play", {U, TP, SG}, n, err) || !ps.add("style_vhs_show", {U, SG, O}, no, err)) return false;
    g.addPass("vhs readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, no](graph::PassContext& x) { x.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(no) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, no, packed, err);
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
bool withinGpu(const Stats& s) { return s.max <= 6 && s.p999 <= 2 && s.mean <= 0.1; }
// Distance in codes of the display value at (x, y, c), built from sig as toDisplay builds it, to its rounding edge.
double edgeAt(const style::VhsPlan& plan, const std::vector<float>& sig, int w, int h, int x, int y, int c) {
    const int N = plan.N, lines = plan.lines;
    const int ly = std::min(lines - 1, int(std::floor(double(y) * lines / h)));
    const double kf = ((x + 0.5) * N) / w - 0.5;
    const int k0 = std::max(0, std::min(N - 1, int(std::floor(kf)))), k1 = std::min(N - 1, k0 + 1);
    const double t = std::min(1.0, std::max(0.0, kf - k0));
    const double v = double(sig[(std::size_t(ly) * N + std::size_t(k0)) * 4 + std::size_t(c)]) * (1 - t) + double(sig[(std::size_t(ly) * N + std::size_t(k1)) * 4 + std::size_t(c)]) * t;
    const double q = std::min(1.0, std::max(0.0, v)) * 255 + 0.5;
    const double fr = q - std::floor(q);
    return std::min(fr, 1 - fr);
}
bool relEq(double a, double b) { return std::abs(a - b) <= 1e-12 * std::max(1.0, std::abs(b)); }
}  // namespace

// The VHS cases; returns the number the bundle holds.
int vhsParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/vhs.rnfx", b, r.error)) return 0;
    const auto src = b.arr<float>("src", "f32");
    const int sw = int(b.head.get("src")->get("width")->number("width")), sh = int(b.head.get("src")->get("height")->number("height"));
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "vhs"; s.id = c.get("id")->s;
        const int w = int(c.get("width")->number("width")), h = int(c.get("height")->number("height")), f = int(c.get("frame")->number("frame"));
        const style::VhsPlan plan = style::resolveVhs(style::vhsPreset(c.get("preset")->s), sw, sh);
        const Json& q = *c.get("plan");
        std::string detail;
        auto check = [&](bool ok, const char* what) { if (!ok) detail += std::string(what) + " "; };
        check(plan.N == int(q.get("N")->number("N")) && plan.lines == int(q.get("lines")->number("lines")) && plan.g == int(q.get("g")->number("g")), "N/lines/g");
        check(relEq(plan.fs, q.get("fs")->number("fs")) && relEq(plan.delay, q.get("delay")->number("delay")), "fs/delay");
        check(relEq(plan.lumaNoise, q.get("lumaNoise")->number("l")) && relEq(plan.chromaNoise, q.get("chromaNoise")->number("c")) && relEq(plan.phaseNoise, q.get("phaseNoise")->number("p")), "noise");
        const auto inv = style::vhsYiqInverse();
        bool invOk = true;
        for (std::size_t k = 0; k < 9; ++k) invOk = invOk && relEq(inv[k], q.get("yiqInv")->at(k).number("inv"));
        check(invOk, "yiqInv");
        const char* tapNames[3] = {"luma", "peak", "chroma"};
        const std::vector<double>* mine[3] = {&plan.luma, &plan.peak, &plan.chroma};
        for (int t = 0; t < 3; ++t) {
            const auto js = b.arr<double>(s.id + ".taps_" + tapNames[t], "f64");
            bool ok = js.size() == mine[t]->size();
            for (std::size_t k = 0; ok && k < js.size(); ++k) ok = relEq((*mine[t])[k], js[k]);
            check(ok, tapNames[t]);
        }
        s.presetEqual = detail.empty();
        s.presetDetail = detail;
        const auto jsSig = b.arr<float>(s.id + ".sig", "f32");
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        style::VhsPlan bentPlan = plan; bentPlan.lumaNoise *= 1.5;
        const std::vector<float> sig = style::vhsFrame(plan, src, f), bentSig = style::vhsFrame(bentPlan, src, f);
        const std::vector<std::uint8_t> disp = style::vhsDisplay(plan, sig, w, h), bentDisp = style::vhsDisplay(bentPlan, bentSig, w, h);
        double mx = 0, bmx = 0;
        for (std::size_t i = 0; i < jsSig.size() && i < sig.size(); ++i) { mx = std::max(mx, std::abs(double(sig[i]) - jsSig[i])); bmx = std::max(bmx, std::abs(double(bentSig[i]) - jsSig[i])); }
        if (sig.size() != jsSig.size()) mx = 1e9;
        s.cpuMaxAbs = mx;
        s.decisions = long(w) * h * 3;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int k = 0; k < 3; ++k) {
            const std::size_t i = (std::size_t(y) * w + std::size_t(x)) * 4 + std::size_t(k);
            if (disp[i] == js[i]) continue;
            (edgeAt(plan, jsSig, w, h, x, y, k) <= 255e-5 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = bmx > 1e-5;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-5 && s.cpuMismatch == 0 && double(s.cpuExcused) <= 1e-4 * double(s.decisions) && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runVhs(*dev, plan, src, f, w, h, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const Stats st = stats(disp, got), sc = stats(bentDisp, got);
            s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean;
            s.gpuControlFails = !withinGpu(sc);
            s.gpuPass = withinGpu(st) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
