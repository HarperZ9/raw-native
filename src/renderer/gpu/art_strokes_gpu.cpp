// P5 on the GPU (evidence/p5-world-strokes-bounds.json w3 and w5): art_anchor, then the painterly
// shader's passes (style_paint_* in style.wgsl) reading its coordinates as the canvas; beside them
// the advected canvas's passes and a paint chain reading those, for the cost comparison.
#include "raw/renderer/art_strokes.hpp"
#include "raw/renderer/style_image.hpp"
#include "art_gpu.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
// PaintGpuParams in style.wgsl (the layout style_parity_paint.cpp declares).
struct PaintParamsGpu { std::uint32_t width, height, medium, canvasMode, dir, strokeLen, blurR, pad0; float a[4], b[4], c[4], d[4], e[4]; };
static_assert(sizeof(PaintParamsGpu) == 112, "PaintGpuParams layout");
struct AnchorParamsGpu { std::uint32_t width, height, pad0, pad1; float a[4]; };
static_assert(sizeof(AnchorParamsGpu) == 32, "AnchorParams layout");
void put(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
PaintParamsGpu paramsOf(const style::PaintPlan& plan, int dir) {
    const style::PaintParams& p = plan.p;
    const double N = 8, kPi = 3.141592653589793, eta = (p.zeta + std::cos(kPi / N)) / std::pow(std::sin(kPi / N), 2);
    PaintParamsGpu P{std::uint32_t(plan.w), std::uint32_t(plan.h), std::uint32_t(p.medium), 1u, std::uint32_t(dir), std::uint32_t(p.strokeLen), std::uint32_t(plan.tw.size() / 2), 0, {}, {}, {}, {}, {}};
    put(P.a, p.exposure, p.radius, p.q, p.zeta); put(P.b, eta, p.impasto, p.gloss, p.push); put(P.c, p.broken, p.valueBands, p.lines, p.lineSigma);
    put(P.d, p.lineSharp, p.warp, p.granulation, p.edge); put(P.e, p.dilution, p.canvasOffset[0], p.canvasOffset[1], 0);
    return P;
}
struct PaintBufs { int U, UX, UY, PG, TW, SC, SB, LAT, T0, T1, OL, OS, HS, RL, O; };
PaintBufs paintBuffers(art_gpu::Runner& R, const style::PaintPlan& plan, const art::Frame& f, const std::vector<float>& pig) {
    const std::uint64_t n = std::uint64_t(plan.w) * std::uint64_t(plan.h);
    PaintBufs b{};
    b.U = R.uniform(paramsOf(plan, 0), "paint params"); b.UX = R.uniform(paramsOf(plan, 0), "paint params x"); b.UY = R.uniform(paramsOf(plan, 1), "paint params y");
    b.PG = R.input(pig, "pigments"); b.TW = R.input(plan.tw, "tensor weights"); b.SC = R.input(f.lit, "scene");
    b.SB = R.buffer(n * 16, "s"); b.LAT = R.buffer(n * 32, "latent"); b.T0 = R.buffer(n * 16, "tensor"); b.T1 = R.buffer(n * 16, "tensor tmp");
    b.OL = R.buffer(n * 32, "akf latent"); b.OS = R.buffer(n * 16, "akf s"); b.HS = R.buffer(n * 8, "stroke"); b.RL = R.buffer(n * 16, "relief");
    b.O = R.buffer(n * 4, "packed");
    return b;
}
bool paintSteps(art_gpu::Runner& R, const PaintBufs& b, int C, int K, std::uint32_t n) {
    return R.pass("style_paint_prep", {b.U, b.PG, b.SC, b.SB, b.LAT}, n) && R.pass("style_paint_tensor", {b.U, b.SB, b.T0}, n) &&
           R.pass("style_paint_blur", {b.UX, b.TW, b.T0, b.T1}, n) && R.pass("style_paint_blur", {b.UY, b.TW, b.T1, b.T0}, n) &&
           R.pass("style_paint_akf", {b.U, b.SB, b.LAT, b.T0, b.OL, b.OS}, n) && R.pass("style_paint_stroke", {b.U, b.T0, b.HS, C, K}, n) &&
           R.pass("style_paint_relief", {b.U, b.OS, b.HS, b.T0, b.RL, C, K}, n) && R.pass("style_paint_compose", {b.U, b.PG, b.OL, b.HS, b.RL, C, K, b.O}, n);
}
}  // namespace

bool strokesOnGpu(rhi::Device& dev, const art::Frame& f, const art::AnchorParams& a, const style::PaintPlan& plan, StrokesGpu& out, int repeats) {
    art_gpu::Runner R(dev);
    const std::uint32_t n = std::uint32_t(f.w) * std::uint32_t(f.h);
    const AnchorParamsGpu AP{std::uint32_t(f.w), std::uint32_t(f.h), 0, 0, {float(a.scale), float(a.focal), 0, 0}};
    const int UA = R.uniform(AP, "anchor params"), PW = R.input(f.P, "P"), NW = R.input(f.N, "N"), DW = R.input(f.dir, "dir"), DS = R.input(f.dist, "dist");
    const int CW = R.buffer(std::uint64_t(n) * 16, "anchor canvas");
    std::vector<float> kw(16, 0.0f), k0(16, 0.0f), mv(std::size_t(n) * 2, 0.0f), dd(std::size_t(n) * 2, 0.0f), pd(n, -1.0f);
    kw[0] = 1e6f; kw[5] = -1.0f; k0[0] = -1.0f; k0[5] = -1.0f;
    for (std::size_t i = 0; i < n; ++i) { const float d = f.dist[i] > 0 ? f.dist[i] : 1e4f; dd[i * 2] = d; dd[i * 2 + 1] = d; }
    const int KW = R.input(kw, "world layers"), KA = R.input(k0, "advected layers"), ST = R.buffer(16, "canvas stats");
    const int C0 = R.buffer(std::uint64_t(n) * 16, "canvas 0"), C1 = R.buffer(std::uint64_t(n) * 16, "canvas 1"), MV = R.input(mv, "motion"), DD = R.input(dd, "dist pair");
    const int PD0 = R.input(pd, "prev dist 0"), PD1 = R.buffer(std::uint64_t(n) * 4, "prev dist 1");
    const PaintBufs pb = paintBuffers(R, plan, f, style::packPigments());
    const bool ok = R.pass("art_anchor", {UA, PW, NW, DW, DS, CW}, n) &&
                    R.passGroups("style_canvas_decide", {pb.U, KA, ST}, 1, 1) && R.pass("style_canvas_advect", {pb.U, KA, C0, C1, MV, DD, PD0, PD1}, n) &&
                    R.pass("style_canvas_distort", {pb.U, C1, ST}, n) && paintSteps(R, pb, C1, KA, n) && paintSteps(R, pb, CW, KW, n);
    std::vector<std::uint32_t> coords;
    if (!ok || !R.run({{CW, &coords}, {pb.O, &out.packed}})) { out.error = R.error(); return false; }
    out.anchor.resize(coords.size());
    for (std::size_t i = 0; i < coords.size(); ++i) std::memcpy(&out.anchor[i], &coords[i], 4);
    if (repeats > 0) {
        const std::size_t paint = 8;
        const int inner = 10;
        double anchorAgain = 0;
        if (!R.time(repeats, out.anchorSingleMs, 0, 1, 1) || !R.time(repeats, out.anchorMs, 0, 1, inner) || !R.time(repeats, out.advectMs, 1, 4, inner) ||
            !R.time(repeats, out.paintAdvectedMs, 1, 4 + paint, inner) || !R.time(repeats, out.paintWorldMs, 4 + paint, 4 + 2 * paint, inner) ||
            !R.time(repeats, anchorAgain, 0, 1, inner)) { out.error = R.error(); return false; }
        out.paintWorldMs += anchorAgain;
    }
    return true;
}

}  // namespace raw::gpu_check
