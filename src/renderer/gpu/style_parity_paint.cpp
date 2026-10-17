// The painterly shader's checks and its GPU runner (for motion cases the canvas passes of every
// frame, then prep, tensor, two blurs, Kuwahara, stroke, relief and compose on the last frame; one
// submission): bounds and controls of evidence/cpp-port-style-bounds-11.json (with Addendum A),
// written before the first run.
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_paint.hpp"
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

struct PaintParamsGpu { std::uint32_t width, height, medium, canvasMode, dir, strokeLen, blurR, pad0; float a[4], b[4], c[4], d[4], e[4]; };
static_assert(sizeof(PaintParamsGpu) == 112, "PaintGpuParams layout");
void put(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
PaintParamsGpu paramsOf(const style::PaintPlan& plan, bool motion, int dir) {
    const style::PaintParams& p = plan.p;
    const double N = 8, kPi = 3.141592653589793, eta = (p.zeta + std::cos(kPi / N)) / std::pow(std::sin(kPi / N), 2);
    PaintParamsGpu P{std::uint32_t(plan.w), std::uint32_t(plan.h), std::uint32_t(p.medium), motion ? 1u : 0u, std::uint32_t(dir), std::uint32_t(p.strokeLen), std::uint32_t(plan.tw.size() / 2), 0, {}, {}, {}, {}, {}};
    put(P.a, p.exposure, p.radius, p.q, p.zeta); put(P.b, eta, p.impasto, p.gloss, p.push); put(P.c, p.broken, p.valueBands, p.lines, p.lineSigma);
    put(P.d, p.lineSharp, p.warp, p.granulation, p.edge); put(P.e, p.dilution, p.canvasOffset[0], p.canvasOffset[1], 0);
    return P;
}
struct MotionFrame { std::vector<float> scene, mv, dist, distPrev; };

bool runPaintGpu(rhi::Device& dev, const style::PaintPlan& plan, const std::vector<float>& scene, const std::vector<MotionFrame>* frames, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const bool motion = frames != nullptr;
    const PaintParamsGpu P = paramsOf(plan, motion, 0), PX = paramsOf(plan, motion, 0), PY = paramsOf(plan, motion, 1);
    const std::uint32_t n = std::uint32_t(plan.w * plan.h);
    const std::vector<float> pig = style::packPigments();
    const float K0[16] = {-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::uint32_t S0[4] = {0, 0, 0, 0};
    const std::vector<float> zeros(std::size_t(n) * 4, 0.0f), pd0(n, -1.0f);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, un = BufferUsage::Uniform | BufferUsage::CopyDst;
    const rhi::BufferHandle ub = gpu.make(sizeof P, un, "paint params"), ux = gpu.make(sizeof P, un, "paint params x"), uy = gpu.make(sizeof P, un, "paint params y");
    const rhi::BufferHandle pb = gpu.make(pig.size() * 4, st, "paint pigments"), twb = gpu.make(plan.tw.size() * 4, st, "paint tensor weights");
    const rhi::BufferHandle scb = gpu.make(std::uint64_t(n) * 16, st, "paint scene"), sbb = gpu.make(std::uint64_t(n) * 16, st, "paint s"), latb = gpu.make(std::uint64_t(n) * 32, st, "paint latent");
    const rhi::BufferHandle t0 = gpu.make(std::uint64_t(n) * 16, st, "paint tensor"), t1 = gpu.make(std::uint64_t(n) * 16, st, "paint tensor tmp");
    const rhi::BufferHandle olb = gpu.make(std::uint64_t(n) * 32, st, "paint akf latent"), osb = gpu.make(std::uint64_t(n) * 16, st, "paint akf s");
    const rhi::BufferHandle hb = gpu.make(std::uint64_t(n) * 8, st, "paint stroke"), rlb = gpu.make(std::uint64_t(n) * 16, st, "paint relief");
    const rhi::BufferHandle kb = gpu.make(64, st, "canvas layers"), sb2 = gpu.make(16, st, "canvas stats");
    rhi::BufferHandle cb[2] = {gpu.make(std::uint64_t(n) * 16, st, "canvas coords 0"), gpu.make(std::uint64_t(n) * 16, st, "canvas coords 1")};
    rhi::BufferHandle pdb[2] = {gpu.make(std::uint64_t(n) * 4, st, "prev dist 0"), gpu.make(std::uint64_t(n) * 4, st, "prev dist 1")};
    const std::size_t nf = motion ? frames->size() : 0;
    std::vector<rhi::BufferHandle> mvb, ddb;
    std::vector<std::vector<float>> dd(nf);
    for (std::size_t f = 0; f < nf; ++f) {
        mvb.push_back(gpu.make(std::uint64_t(n) * 8, st, "motion"));
        ddb.push_back(gpu.make(std::uint64_t(n) * 8, st, "dist and dist prev"));
        dd[f].resize(std::size_t(n) * 2);
        for (std::size_t i = 0; i < n; ++i) { dd[f][i * 2] = (*frames)[f].dist[i]; dd[f][i * 2 + 1] = (*frames)[f].distPrev[i]; }
    }
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "paint packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "paint readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    auto imp = [&](const char* name, rhi::BufferHandle h) { return g.importBuffer(name, h); };
    const graph::Resource U = imp("params", ub), UX = imp("params x", ux), UY = imp("params y", uy), PG = imp("pig", pb), TW = imp("tw", twb), SC = imp("scene", scb);
    const graph::Resource SB = imp("s", sbb), LAT = imp("lat", latb), T0 = imp("T0", t0), T1 = imp("T1", t1), OL = imp("olat", olb), OS = imp("os", osb), HS = imp("H", hb), RL = imp("Rl", rlb);
    const graph::Resource K = imp("K", kb), S = imp("S", sb2), C[2] = {imp("C0", cb[0]), imp("C1", cb[1])}, PD[2] = {imp("pd0", pdb[0]), imp("pd1", pdb[1])};
    std::vector<graph::Resource> MV, DD;
    for (std::size_t f = 0; f < nf; ++f) { MV.push_back(imp("mv", mvb[f])); DD.push_back(imp("dd", ddb[f])); }
    const graph::Resource O = imp("out8", ob), RB = imp("readback", rb);
    std::vector<graph::Use> up = {{U, Access::CopyDst}, {UX, Access::CopyDst}, {UY, Access::CopyDst}, {PG, Access::CopyDst}, {TW, Access::CopyDst}, {SC, Access::CopyDst},
                                  {K, Access::CopyDst}, {S, Access::CopyDst}, {C[0], Access::CopyDst}, {C[1], Access::CopyDst}, {PD[0], Access::CopyDst}};
    for (std::size_t f = 0; f < nf; ++f) { up.push_back({MV[f], Access::CopyDst}); up.push_back({DD[f], Access::CopyDst}); }
    g.addPass("paint upload", up, [&](graph::PassContext& c) {
        c.cmd->upload(ub, 0, &P, sizeof P); c.cmd->upload(ux, 0, &PX, sizeof P); c.cmd->upload(uy, 0, &PY, sizeof P);
        c.cmd->upload(pb, 0, pig.data(), pig.size() * 4); c.cmd->upload(twb, 0, plan.tw.data(), plan.tw.size() * 4); c.cmd->upload(scb, 0, scene.data(), scene.size() * 4);
        c.cmd->upload(kb, 0, K0, sizeof K0); c.cmd->upload(sb2, 0, S0, sizeof S0);
        c.cmd->upload(cb[0], 0, zeros.data(), zeros.size() * 4); c.cmd->upload(cb[1], 0, zeros.data(), zeros.size() * 4); c.cmd->upload(pdb[0], 0, pd0.data(), pd0.size() * 4);
        for (std::size_t f = 0; f < nf; ++f) { c.cmd->upload(mvb[f], 0, (*frames)[f].mv.data(), std::size_t(n) * 8); c.cmd->upload(ddb[f], 0, dd[f].data(), std::size_t(n) * 8); }
    });
    style_graph::Passes ps{dev, g, {}};
    int cur = 0;   // the canvas buffers last written
    for (std::size_t f = 0; f < nf; ++f) {
        const int nxt = 1 - cur;
        if (!ps.addGroups("style_canvas_decide", {U, K, S}, 1, 1, 1, err) || !ps.add("style_canvas_advect", {U, K, C[cur], C[nxt], MV[f], DD[f], PD[cur], PD[nxt]}, n, err) ||
            !ps.add("style_canvas_distort", {U, C[nxt], S}, n, err)) return false;
        cur = nxt;
    }
    if (!ps.add("style_paint_prep", {U, PG, SC, SB, LAT}, n, err) || !ps.add("style_paint_tensor", {U, SB, T0}, n, err) ||
        !ps.add("style_paint_blur", {UX, TW, T0, T1}, n, err) || !ps.add("style_paint_blur", {UY, TW, T1, T0}, n, err) ||
        !ps.add("style_paint_akf", {U, SB, LAT, T0, OL, OS}, n, err) || !ps.add("style_paint_stroke", {U, T0, HS, C[cur], K}, n, err) ||
        !ps.add("style_paint_relief", {U, OS, HS, T0, RL, C[cur], K}, n, err) || !ps.add("style_paint_compose", {U, PG, OL, HS, RL, C[cur], K, O}, n, err)) return false;
    g.addPass("paint readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

struct Stats { int max{0}, p999{0}; double mean{0}; long over2{0}, differ{0}; };
Stats stats(const std::vector<std::uint8_t>& want, const std::vector<std::uint8_t>& got) {
    long hist[256] = {}, n = 0; double sum = 0; Stats s;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if ((i & 3) == 3) continue;
        const int d = std::abs(int(want[i]) - int(got[i]));
        hist[d]++; sum += d; n++; s.max = std::max(s.max, d); s.over2 += d > 2; s.differ += d > 0;
    }
    s.mean = sum / double(n);
    long acc = 0;
    for (int d = 0; d < 256; ++d) { acc += hist[d]; if (double(acc) >= double(n) * 0.999) { s.p999 = d; break; } }
    return s;
}
bool gpuWithin(const std::string& set, const Stats& s) {
    if (set == "stills") return s.max <= 8 && s.p999 <= 2 && s.mean <= 0.1;
    if (set == "motion") return s.max <= 8 && s.p999 <= 3 && s.mean <= 0.2;
    return s.max <= 4 && s.over2 == 0 && s.mean <= 0.05;
}
std::vector<std::uint8_t> codesOf(const std::vector<float>& img) {
    std::vector<std::uint8_t> o(img.size());
    for (std::size_t i = 0; i < img.size(); ++i) o[i] = (i & 3) == 3 ? 255 : style::paintEncode8(img[i]);
    return o;
}
std::vector<std::uint8_t> unpack(const std::vector<std::uint32_t>& w) { std::vector<std::uint8_t> o(w.size() * 4); std::memcpy(o.data(), w.data(), o.size()); return o; }
style::PaintParams paramsFor(const Json& c) {
    style::PaintParams p = style::paintPreset(c.get("preset")->s);
    const Json& ov = *c.get("overrides");
    if (const Json* v = ov.get("exposure")) p.exposure = v->number("exposure");
    if (const Json* v = ov.get("canvasOffset")) p.canvasOffset = {v->at(0).number("o"), v->at(1).number("o")};
    return p;
}
}  // namespace

// The paint cases; returns the number the bundle holds.
int paintParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/paint.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "paint"; s.id = c.get("id")->s;
        const std::string set = c.get("set")->s;
        const int w = int(c.get("width")->number("w")), h = int(c.get("height")->number("h"));
        const style::PaintParams p = paramsFor(c);
        style::PaintParams pb = p; pb.exposure *= 1.1;
        const style::PaintPlan plan = style::resolvePaint(p, w, h), bentPlan = style::resolvePaint(pb, w, h);
        bool twEq = plan.tw.size() == b.head.get("tw")->a.size();
        for (std::size_t i = 0; twEq && i < plan.tw.size(); ++i) twEq = plan.tw[i] == float(b.head.get("tw")->at(i).number("tw"));
        std::string detail = twEq ? "" : "tensor weights ";
        std::vector<float> scene, img, bent;
        std::vector<MotionFrame> frames;
        if (set == "motion") {
            const int nfr = int(c.get("frames")->number("frames"));
            style::PaintCanvas cv(w, h);
            for (int f = 0; f < nfr; ++f) {
                const std::string pre = s.id + ".f" + std::to_string(f) + ".";
                MotionFrame m{b.arr<float>(pre + "scene", "f32"), b.arr<float>(pre + "mv", "f32"), b.arr<float>(pre + "dist", "f32"), b.arr<float>(pre + "distPrev", "f32")};
                style::paintDecide(cv);
                style::paintAdvect(cv, m.mv, m.dist, m.distPrev);
                frames.push_back(std::move(m));
            }
            scene = frames.back().scene;
            bool layersEq = true;
            for (int k = 0; k < 2; ++k) {
                const Json& L = c.get("layers")->at(std::size_t(k));
                const style::PaintLayer& l = cv.layer[std::size_t(k)];
                layersEq = layersEq && l.age == L.get("age")->number("age") && std::abs(l.meanD - L.get("meanD")->number("m")) <= 1e-12 &&
                           l.seed[0] == L.get("seed")->at(0).number("s") && l.seed[1] == L.get("seed")->at(1).number("s");
            }
            if (!layersEq) detail += "canvas layers ";
            img = style::runPaint(plan, scene, &cv);
            bent = style::runPaint(bentPlan, scene, &cv);
        } else {
            scene = b.arr<float>(c.get("scene")->s, "f32");
            img = style::runPaint(plan, scene, nullptr);
            bent = style::runPaint(bentPlan, scene, nullptr);
        }
        s.presetEqual = detail.empty(); s.presetDetail = detail;
        const auto js = b.arr<std::uint8_t>(s.id + ".js", "u8");
        const std::vector<std::uint8_t> codes = codesOf(img), bcodes = codesOf(bent);
        double mx = 0, bmx = 0;
        const bool hasImg = b.head.get("arrays")->get(s.id + ".img") != nullptr;
        if (hasImg) {
            const auto jsImg = b.arr<float>(s.id + ".img", "f32");
            for (std::size_t i = 0; i < jsImg.size(); ++i) { if ((i & 3) == 3) continue; mx = std::max(mx, std::abs(double(img[i]) - jsImg[i])); bmx = std::max(bmx, std::abs(double(bent[i]) - jsImg[i])); }
        }
        const Stats cs = stats(js, codes), cb = stats(js, bcodes);
        s.decisions = long(js.size() / 4 * 3);
        s.cpuMaxAbs = mx; s.cpuMismatch = cs.differ; s.cpuRgbaMismatch = cs.max;
        const bool cpuCodes = double(cs.differ) <= 1e-4 * double(s.decisions) && cs.max <= 1;
        s.cpuControlFails = (hasImg && bmx > 1e-4) || double(cb.differ) > 1e-4 * double(s.decisions) || cb.max > 1;
        s.cpuPass = s.presetEqual && (!hasImg || mx <= 1e-4) && cpuCodes && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runPaintGpu(*dev, plan, scene, set == "motion" ? &frames : nullptr, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            const std::vector<std::uint8_t> g8 = unpack(got);
            const Stats gs = stats(codes, g8), gb = stats(bcodes, g8);
            s.gpuMax = gs.max; s.gpuP999 = gs.p999; s.gpuMean = gs.mean; s.gpuOver2Fraction = double(gs.over2) / double(s.decisions);
            s.gpuControlFails = !gpuWithin(set, gb);
            s.gpuPass = gpuWithin(set, gs) && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
