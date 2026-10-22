// P23 on the GPU: see raw/renderer/art_tiltshift_gpu.hpp.
#include "raw/renderer/art_tiltshift_gpu.hpp"
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_paint.hpp"
#include "art_gpu.hpp"
#include <cstring>
namespace raw::gpu_check {
namespace {
struct TsParamsGpu {
    std::uint32_t width, height, qw, qh, dir, seed, blurR, pad0;
    float eye[4], fwd[4], pn[4], lens[4], dp[4], fx0[4], fx1[4], fx2[4], fx3[4], fx4[4], sd0[4], sd1[4];
};
static_assert(sizeof(TsParamsGpu) == 224, "TsParams layout");
void put(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
TsParamsGpu paramsOf(const art::LensGeom& g, const art::FxPlan& P, std::uint32_t blurR, std::uint32_t dir) {
    const art::TiltLook& L = P.look;
    const art::DefocusParams dp;
    TsParamsGpu u{std::uint32_t(P.w), std::uint32_t(P.h), std::uint32_t(P.qw), std::uint32_t(P.qh), dir, P.seed, blurR, 0, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
    put(u.eye, g.eye[0], g.eye[1], g.eye[2], g.fpx); put(u.fwd, g.fwd[0], g.fwd[1], g.fwd[2], g.fmm); put(u.pn, g.n[0], g.n[1], g.n[2], g.d);
    put(u.lens, g.mmPerUnit, g.N, g.cMax, 0); put(u.dp, dp.c0, dp.c1, dp.q, dp.rim);
    put(u.fx0, L.vignette, L.mech, L.halation, L.star); put(u.fx1, L.ca, L.grain, L.print, L.halftone); put(u.fx2, L.misreg, P.weaveX, P.weaveY, P.cell);
    put(u.fx3, P.haloR, P.starStep, P.thr, P.knee); put(u.fx4, P.fr.reduceFlashes ? 0.5 : 1.0, 0, 0, 0);
    put(u.sd0, P.dirs[0], P.dirs[1], P.dirs[2], P.dirs[3]); put(u.sd1, P.dirs[4], P.dirs[5], P.dirs[6], P.dirs[7]);
    return u;
}
}  // namespace

bool tiltOnGpu(rhi::Device& dev, const art::Frame& f, const art::LensGeom& g, const art::FxPlan& P, const std::vector<float>& paint, TiltGpu& out, int repeats) {
    art_gpu::Runner R(dev);
    const std::uint32_t n = std::uint32_t(f.w) * std::uint32_t(f.h), q = std::uint32_t(P.qw) * std::uint32_t(P.qh);
    const std::vector<float> tw = style::paintGaussWeights(2.0);
    std::vector<float> tables = style::packPigments();
    if (tables.size() != 19993) { out.error = "pigment table size changed; art_ts_defocus reads the sample table at 19993"; return false; }
    const art::DefocusTables& dt = art::defocusTables();
    tables.insert(tables.end(), dt.pos.begin(), dt.pos.end());
    tables.insert(tables.end(), dt.wk.begin(), dt.wk.end());
    const int UA = R.uniform(paramsOf(g, P, std::uint32_t(tw.size() / 2), 0), "ts params"), UB = R.uniform(paramsOf(g, P, std::uint32_t(tw.size() / 2), 1), "ts params y");
    const int DS = R.input(f.dist, "dist"), DW = R.input(f.dir, "dir"), PIG = R.input(tables, "pigments and samples"), IMG = R.input(paint, "paint");
    const int TW = R.input(tw, "tensor weights"), HT = R.input(art::haloTable(), "halo taps");
    const int C = R.buffer(std::uint64_t(n) * 16, "coc"), CW = R.buffer(std::uint64_t(n) * 16, "coc weight"), SB = R.buffer(std::uint64_t(n) * 16, "srgb");
    const int LAT = R.buffer(std::uint64_t(n) * 32, "latent"), T0 = R.buffer(std::uint64_t(n) * 16, "tensor"), T1 = R.buffer(std::uint64_t(n) * 16, "tensor tmp");
    const int OUT = R.buffer(std::uint64_t(n) * 16, "defocused"), BQ = R.buffer(std::uint64_t(q) * 16, "bright"), GQ = R.buffer(std::uint64_t(q) * 32, "glow");
    const int O8 = R.buffer(std::uint64_t(n) * 4, "codes");
    const bool ok = R.pass("art_ts_coc", {UA, DS, DW, C}, n) && R.pass("art_ts_weight", {UA, C, CW}, n) && R.pass("art_ts_prep", {UA, PIG, IMG, SB, LAT}, n) &&
                    R.pass("art_ts_tensor", {UA, SB, T0}, n) && R.pass("art_ts_blur", {UA, TW, T0, T1}, n) && R.pass("art_ts_blur", {UB, TW, T1, T0}, n) &&
                    R.pass("art_ts_defocus", {UA, PIG, IMG, SB, LAT, T0, CW, OUT}, n) && R.pass("art_ts_bright", {UA, OUT, BQ}, q) &&
                    R.pass("art_ts_glow", {UA, HT, BQ, GQ}, q) && R.pass("art_ts_finish", {UA, OUT, GQ, O8}, n);
    std::vector<std::uint32_t> cw;
    if (!ok || !R.run({{C, &cw}, {O8, &out.packed}})) { out.error = R.error(); return false; }
    out.coc.resize(n);
    for (std::size_t i = 0; i < n; ++i) std::memcpy(&out.coc[i], &cw[i * 4], 4);
    if (repeats > 0 && !R.timeStamps(repeats, out.chainMs)) { out.error = R.error(); return false; }
    return true;
}

}  // namespace raw::gpu_check
