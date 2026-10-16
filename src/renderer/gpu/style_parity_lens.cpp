// The lens suite's checks and its GPU runner (core and gather, one submission): bounds and
// controls of evidence/cpp-port-style-bounds-7.json, written before the first run.
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_lens.hpp"
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

struct LensParamsGpu { std::uint32_t width, height, nSrc, nGhost, starM, tile, tilesX, blades; float a[4], b[4], c[4], phi[4]; };
static_assert(sizeof(LensParamsGpu) == 96, "LensGpuParams layout");
void put(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }

bool runLensGpu(rhi::Device& dev, const style::LensPlan& plan, const std::vector<double>& frame, const std::vector<style::LensSource>& srcs,
                const std::vector<std::uint8_t>& keepIn, double gain, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const style::LensParams& p = plan.p;
    const int tw = (plan.W + p.tile - 1) / p.tile, th = (plan.H + p.tile - 1) / p.tile;
    LensParamsGpu P{std::uint32_t(plan.W), std::uint32_t(plan.H), std::uint32_t(srcs.size()), std::uint32_t(plan.ghosts.size()), std::uint32_t(plan.star.M),
                    std::uint32_t(p.tile), std::uint32_t(tw), std::uint32_t(p.iris.blades), {}, {}, {}, {}};
    put(P.a, plan.cx, plan.cy, p.squeeze, p.iris.roundness);
    put(P.b, std::cos(p.streakAngle), std::sin(p.streakAngle), p.streakLenPx, p.streakThickPx);
    put(P.c, plan.starCell, p.starGain, p.threshold, gain);
    put(P.phi, plan.phi[0], plan.phi[1], plan.phi[2], 0);
    std::vector<float> img, bins, gh, sv(std::max<std::size_t>(1, srcs.size()) * 24, 0.0f), star;
    for (double v : frame) img.push_back(float(v));
    for (double v : plan.star.data) star.push_back(float(v));
    for (const auto& w : plan.binRgb) for (double v : w) bins.push_back(float(v));
    for (double v : plan.caScale) bins.push_back(float(v));
    for (const auto& hh : plan.halo) { bins.push_back(float(hh[0])); bins.push_back(float(hh[1])); }
    for (const auto& g : plan.ghosts) for (const auto& gl : g) for (double v : gl) gh.push_back(float(v));
    for (std::size_t k = 0; k < srcs.size(); ++k) {
        float* o = &sv[k * 24];
        o[0] = float(srcs[k].x); o[1] = float(srcs[k].y);
        for (int c = 0; c < 3; ++c) o[2 + c] = float(srcs[k].E[std::size_t(c)]);
        for (int l = 0; l < 8; ++l) { o[5 + l] = float(srcs[k].ghost[std::size_t(l)]); o[13 + l] = float(srcs[k].streak[std::size_t(l)]); }
    }
    std::vector<std::uint32_t> keep(std::size_t(tw) * th, 0u);
    for (std::size_t i = 0; i < keepIn.size() && i < keep.size(); ++i) keep[i] = keepIn[i];
    const std::uint32_t n = std::uint32_t(plan.W * plan.H);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, BufferUsage::Uniform | BufferUsage::CopyDst, "lens params"), ib = gpu.make(img.size() * 4, st, "lens in");
    const rhi::BufferHandle kb = gpu.make(keep.size() * 4, st, "lens keep"), cb = gpu.make(std::uint64_t(n) * 16, st, "lens core");
    const rhi::BufferHandle gb = gpu.make(gh.size() * 4, st, "lens ghosts"), sb = gpu.make(sv.size() * 4, st, "lens sources");
    const rhi::BufferHandle tb = gpu.make(star.size() * 4, st, "lens star"), bb = gpu.make(bins.size() * 4, st, "lens bins");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "lens out8"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "lens readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    const graph::Resource U = g.importBuffer("params", ub), I = g.importBuffer("in", ib), K = g.importBuffer("keep", kb), C = g.importBuffer("core", cb);
    const graph::Resource G = g.importBuffer("ghosts", gb), S = g.importBuffer("srcs", sb), T = g.importBuffer("star", tb), B = g.importBuffer("bins", bb);
    const graph::Resource O = g.importBuffer("out8", ob), RB = g.importBuffer("readback", rb);
    g.addPass("lens upload", {{U, Access::CopyDst}, {I, Access::CopyDst}, {K, Access::CopyDst}, {G, Access::CopyDst}, {S, Access::CopyDst}, {T, Access::CopyDst}, {B, Access::CopyDst}},
              [&](graph::PassContext& c) {
                  c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(ib, 0, img.data(), img.size() * 4); c.cmd->upload(kb, 0, keep.data(), keep.size() * 4);
                  c.cmd->upload(gb, 0, gh.data(), gh.size() * 4); c.cmd->upload(sb, 0, sv.data(), sv.size() * 4);
                  c.cmd->upload(tb, 0, star.data(), star.size() * 4); c.cmd->upload(bb, 0, bins.data(), bins.size() * 4);
              });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_lens_core", {U, I, K, C}, n, err) || !ps.add("style_lens_gather", {U, C, G, S, T, B, O}, n, err)) return false;
    g.addPass("lens readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
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
bool withinGpu(const Stats& s) { return s.max <= 3 && s.p999 <= 1 && s.mean <= 0.03; }
std::vector<std::uint8_t> codesOf(const std::vector<double>& v, double gain) {
    std::vector<std::uint8_t> o(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::encodeLab8(v[i], gain);
    return o;
}
double maxRel(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return 1e9;
    double m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if ((i & 3) != 3) m = std::max(m, std::abs(a[i] - b[i]) / std::max(1.0, std::abs(b[i])));
    return m;
}
bool rel(double a, double b, double tol = 1e-12) { return std::abs(a - b) <= tol * std::max(std::abs(b), 1e-300); }
std::array<double, 3> e3(const Json& a) { return {a.at(0).number("e"), a.at(1).number("e"), a.at(2).number("e")}; }
style::LensParams paramsOf(const Json& j) {
    style::LensParams p;
    auto num = [&](const char* k, double& v) { if (const Json* x = j.get(k)) v = x->number(k); };
    num("squeeze", p.squeeze); num("caAmount", p.caAmount); num("fNumber", p.fNumber); num("streakAngle", p.streakAngle); num("ghostGain", p.ghostGain);
    if (const Json* ir = j.get("iris")) {
        if (const Json* v = ir->get("blades")) p.iris.blades = int(v->number("blades"));
        if (const Json* v = ir->get("roundness")) p.iris.roundness = v->number("roundness");
        if (const Json* v = ir->get("rotation")) p.iris.rotation = v->number("rotation");
    }
    if (const Json* co = j.get("coating")) {
        if (const Json* v = co->get("quarterAt")) p.coating.quarterAt = v->number("q");
        if (const Json* v = co->get("n1")) p.coating.n1 = v->number("n1");
        if (const Json* v = co->get("ns")) p.coating.ns = v->number("ns");
    }
    return p;
}
}  // namespace

// The lens cases; returns the number the bundle holds.
int lensParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/lens.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "lens"; s.id = c.get("id")->s;
        const int W = int(c.get("width")->number("w")), H = int(c.get("height")->number("h"));
        const double gain = c.get("gain")->number("gain");
        const style::LensParams p = paramsOf(*c.get("params"));
        const style::LensPlan plan = style::resolveLens(p, W, H);
        const Json& q = *c.get("plan");
        std::string detail;
        auto chk = [&](bool ok, const char* what) { if (!ok) detail += std::string(what) + " "; };
        chk(rel(plan.D, q.get("D")->number("D")) && rel(plan.tanHalf, q.get("tanHalf")->number("t")) && rel(plan.Rbar, q.get("Rbar")->number("R")) && rel(plan.starCell, q.get("starCell")->number("s")), "D/tanHalf/Rbar/starCell");
        chk(rel(plan.p.streakLenPx, q.get("streakLenPx")->number("l")) && rel(plan.p.streakThickPx, q.get("streakThickPx")->number("t")) && rel(plan.p.minGhostPx, q.get("minGhostPx")->number("m")) && rel(plan.p.haloRadiusPx, q.get("haloRadiusPx")->number("h")), "lengths");
        bool ok = true;
        for (int l = 0; l < 8; ++l) {
            ok = ok && rel(plan.R0[std::size_t(l)], q.get("R0")->at(std::size_t(l)).number("r")) && rel(plan.caScale[std::size_t(l)], q.get("caScale")->at(std::size_t(l)).number("c"));
            ok = ok && rel(plan.halo[std::size_t(l)][0], q.get("halo")->at(std::size_t(l)).at(0).number("h")) && rel(plan.halo[std::size_t(l)][1], q.get("halo")->at(std::size_t(l)).at(1).number("h"));
            for (int k = 0; k < 3; ++k) ok = ok && rel(plan.binRgb[std::size_t(l)][std::size_t(k)], q.get("binRgb")->at(std::size_t(l)).at(std::size_t(k)).number("w"));
        }
        chk(ok, "bins");
        chk(rel(plan.phi[0], q.get("phi")->at(0).number("p")) && rel(plan.phi[1], q.get("phi")->at(1).number("p")) && rel(plan.phi[2], q.get("phi")->at(2).number("p")), "phi");
        { const auto js = b.arr<double>(s.id + ".ghosts", "f64"); std::vector<double> mine; for (const auto& g : plan.ghosts) for (const auto& gl : g) for (double v : gl) mine.push_back(v);
          bool g = js.size() == mine.size(); for (std::size_t i = 0; g && i < js.size(); ++i) g = rel(mine[i], js[i]); chk(g, "ghosts"); }
        { const auto js = b.arr<double>(s.id + ".star", "f64"); double top = 0; for (double v : js) top = std::max(top, std::abs(v));
          bool g = plan.star.M == int(q.get("starM")->number("M")) && js.size() == plan.star.data.size(); for (std::size_t i = 0; g && i < js.size(); ++i) g = std::abs(plan.star.data[i] - js[i]) <= 1e-9 * top; chk(g, "star"); }
        const auto f32 = b.arr<float>(s.id + ".frame", "f32");
        const std::vector<double> frame(f32.begin(), f32.end());
        std::vector<style::LensRawSource> list;
        std::vector<std::uint8_t> keep;
        const bool listMode = c.get("mode")->s == "list";
        if (listMode) { for (const Json& e : c.get("list")->a) list.push_back({e.get("x")->number("x"), e.get("y")->number("y"), e3(*e.get("E"))}); }
        else {
            list = style::extractLensSources(plan, frame, keep);
            const auto jsKeep = b.arr<std::uint8_t>(s.id + ".keep", "u8");
            bool g = jsKeep == keep && list.size() == c.get("list")->a.size();
            for (std::size_t k = 0; g && k < list.size(); ++k) {
                const Json& e = c.get("list")->at(k);
                g = rel(list[k].x, e.get("x")->number("x")) && rel(list[k].y, e.get("y")->number("y"));
                for (int ch = 0; g && ch < 3; ++ch) g = rel(list[k].E[std::size_t(ch)], e.get("E")->at(std::size_t(ch)).number("E"));
            }
            chk(g, "sources");
        }
        const std::vector<style::LensSource> srcs = style::prepareLensSources(plan, list);
        { bool g = srcs.size() == c.get("srcs")->a.size();
          for (std::size_t k = 0; g && k < srcs.size(); ++k) { const Json& e = c.get("srcs")->at(k); g = rel(srcs[k].theta, e.get("theta")->number("t")) || (srcs[k].theta == 0 && e.get("theta")->number("t") == 0);
            for (int l = 0; g && l < 8; ++l) g = rel(srcs[k].ghost[std::size_t(l)], e.get("ghost")->at(std::size_t(l)).number("g")) && rel(srcs[k].streak[std::size_t(l)], e.get("streak")->at(std::size_t(l)).number("s")); }
          chk(g, "prepared"); }
        s.presetEqual = detail.empty(); s.presetDetail = detail;
        const auto js = b.arr<double>(s.id + ".js", "f64");
        style::LensParams pb = p; pb.ghostGain *= 1.25;
        const style::LensPlan bentPlan = style::resolveLens(pb, W, H);
        const std::vector<double> cpp = style::runLens(plan, frame, srcs, keep), bent = style::runLens(bentPlan, frame, style::prepareLensSources(bentPlan, list), keep);
        s.decisions = long(js.size() / 4);
        s.cpuMaxAbs = maxRel(cpp, js);
        for (std::size_t i = 0; i < js.size(); ++i) {
            if ((i & 3) == 3 || style::encodeLab8(cpp[i], gain) == style::encodeLab8(js[i], gain)) continue;
            (style::edgeLab8(cpp[i], gain) <= 1e-9 ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = maxRel(bent, js) > 1e-9;
        s.cpuPass = s.presetEqual && s.cpuMaxAbs <= 1e-9 && s.cpuMismatch == 0 && s.cpuExcused <= 2 && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runLensGpu(*dev, plan, frame, srcs, keep, gain, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const Stats st = stats(codesOf(cpp, gain), got), sc = stats(codesOf(bent, gain), got);
            s.gpuMax = st.max; s.gpuP999 = st.p999; s.gpuMean = st.mean;
            s.gpuControlFails = !withinGpu(sc);
            s.gpuPass = withinGpu(st) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
