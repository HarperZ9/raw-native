// The ENB CRT and VHS port on the GPU: art_enbcrt_down, then art_enbcrt (evidence/art-enb-crt-bounds.json).
#include "raw/renderer/art_enbcrt.hpp"
#include "art_gpu.hpp"
namespace raw::gpu_check {
namespace {
struct EnbCrtParamsGpu { std::uint32_t width, height, frame, flags, maskType, profile, quality, generation; float a[4], b[4], c[4], d[4], e[4], f[4]; };
static_assert(sizeof(EnbCrtParamsGpu) == 128, "EnbCrtParams layout");
void put(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
EnbCrtParamsGpu pack(const art::EnbCrtParams& p, int w, int h, int frame) {
    EnbCrtParamsGpu g{std::uint32_t(w), std::uint32_t(h), std::uint32_t(frame), (p.crt ? 1u : 0u) | (p.vhs ? 2u : 0u),
                      std::uint32_t(p.maskType), std::uint32_t(p.colourProfile), std::uint32_t(p.quality), std::uint32_t(p.generation), {}, {}, {}, {}, {}, {}};
    put(g.a, p.curvature, p.overscan, p.brightness, p.contrast); put(g.b, p.saturation, p.scanIntensity, p.scanWidth, p.beamWidth);
    put(g.c, p.maskIntensity, p.maskScale, p.bloom, p.convergence); put(g.d, p.halation, p.cornerRadius, p.signalNoise, frame / 30.0);
    put(g.e, p.chromaBlur, p.chromaSmear, p.tracking, p.headSwitch); put(g.f, p.tapeNoise, p.snow, p.jitter, 0);
    return g;
}
}  // namespace

bool enbCrtOnGpu(rhi::Device& dev, const art::EnbCrtParams& p, const std::vector<float>& img, int w, int h, int frame, EnbCrtGpu& out, int repeats) {
    art_gpu::Runner R(dev);
    const std::uint32_t n = std::uint32_t(w) * std::uint32_t(h);
    const int U = R.uniform(pack(p, w, h, frame), "enbcrt params"), S = R.input(img, "enbcrt source");
    const int D = R.buffer(128 * 128 * 16, "enbcrt down"), O = R.buffer(std::uint64_t(n) * 4, "enbcrt out");
    if (!R.pass("art_enbcrt_down", {U, S, D}, 128 * 128) || !R.pass("art_enbcrt", {U, S, D, O}, n) || !R.run({{O, &out.packed}})) { out.error = R.error(); return false; }
    if (repeats > 0 && !R.time(repeats, out.ms, 0, 2, 10)) { out.error = R.error(); return false; }
    return true;
}

}  // namespace raw::gpu_check
