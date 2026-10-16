// The image passes of round 2 on an RHI device: see style_gpu_image.hpp.
#include "style_gpu_image.hpp"
#include "style_gpu.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
namespace raw::gpu_check::style_gpu {
namespace {
using rhi::Access;
using rhi::BufferUsage;
void put4(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
void put3(float* o, const style::V3& v, double w = 0) { put4(o, v[0], v[1], v[2], w); }
// Fog fraction, kind and view distance a pixel (web/shaders/lightpaint/gpu.mjs packAux).
std::vector<float> aux(const style::LitFrame& f) {
    const std::size_t n = std::size_t(f.w) * f.h;
    std::vector<float> a(n * 4, 0.0f);
    for (std::size_t i = 0; i < n; ++i) { a[i * 4] = f.fogT[i]; a[i * 4 + 1] = float(f.kind[i]); a[i * 4 + 2] = f.depth[i]; }
    return a;
}
bool finish(bool ok, const util::Gpu& gpu, const char* what, std::string& err) {
    if (!ok) err = gpu.err.empty() ? std::string(what) + " dispatch failed" : gpu.err;
    return ok;
}
}  // namespace

bool runPurity(rhi::Device& dev, const style::PurityPlan& p, int w, int h, const std::vector<float>& src, double t,
               std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(w * h);
    PurityParamsGpu P{std::uint32_t(w), std::uint32_t(h), 0, 0, {}, {}, {}, {}};
    put4(P.a, p.s, p.w, p.mag, p.magScale); put4(P.b, p.earth, p.degauss, p.tau, p.mains);
    put4(P.c, p.field, p.blank, p.wobble, t); put4(P.d, p.spread, 0, 0, 0);
    const auto ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "purity params");
    const auto sb = gpu.upload(src.data(), src.size() * 4, BufferUsage::Storage, "purity source");
    const auto o8 = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "purity packed");
    return finish(dispatchAndRead(gpu, passIndex("style_purity"), {ub, sb, o8}, {Access::Uniform, Access::StorageRead, Access::StorageWrite}, {o8}, n, {&packed}),
                  gpu, "purity", err);
}

bool runLightpaint(rhi::Device& dev, const style::LightpaintPlan& p, const style::LitFrame& f, double gain,
                   std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(f.w * f.h);
    LightpaintParamsGpu P{std::uint32_t(f.w), std::uint32_t(f.h), p.complement ? 1u : 0u, 0, {}, {}, {}, {}};
    put4(P.a, p.Emid, p.t0, p.strength, p.shape); put4(P.shadow, p.shadow[0], p.shadow[1], p.shadow[2], p.shadow[3]);
    put4(P.light, p.light[0], p.light[1], p.light[2], p.light[3]); put3(P.fog, f.fogColor, gain);
    const std::vector<float> pig = style::packPigments(), ax = aux(f);
    const auto ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "lp params");
    const auto pb = gpu.upload(pig.data(), pig.size() * 4, BufferUsage::Storage, "lp pigments");
    const auto ab = gpu.upload(f.albedo.data(), f.albedo.size() * 4, BufferUsage::Storage, "lp albedo");
    const auto lb = gpu.upload(f.light.data(), f.light.size() * 4, BufferUsage::Storage, "lp irradiance");
    const auto xb = gpu.upload(ax.data(), ax.size() * 4, BufferUsage::Storage, "lp aux");
    const auto o8 = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "lp packed");
    const Access R = Access::StorageRead;
    return finish(dispatchAndRead(gpu, passIndex("style_lightpaint"), {ub, pb, ab, lb, xb, o8}, {Access::Uniform, R, R, R, R, Access::StorageWrite}, {o8}, n, {&packed}),
                  gpu, "lightpaint", err);
}

bool runGlaze(rhi::Device& dev, const style::GlazePlan& p, const style::LitFrame& f, double gain,
              std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(f.w * f.h);
    const style::LitCamera& c = f.camera;
    GlazeParamsGpu P{std::uint32_t(f.w), std::uint32_t(f.h), p.dispersion ? 1u : 0u, 0, {}, {}, {}, {}, {}, {}, {}, {}};
    put4(P.a, p.d0, p.dVar, p.noiseScale, p.coverage); put4(P.b, p.n0, p.n2, p.A, p.B); put4(P.c, p.filmN, c.aspect, c.tan, gain);
    put3(P.fwd, c.fwd); put3(P.right, c.right); put3(P.up, c.up); put3(P.eye, c.eye); put3(P.fog, f.fogColor);
    const std::vector<float> pig = style::packPigments(), ax = aux(f);
    const auto ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "glaze params");
    const auto pb = gpu.upload(pig.data(), pig.size() * 4, BufferUsage::Storage, "glaze pigments");
    const auto ab = gpu.upload(f.albedo.data(), f.albedo.size() * 4, BufferUsage::Storage, "glaze albedo");
    const auto lb = gpu.upload(f.light.data(), f.light.size() * 4, BufferUsage::Storage, "glaze irradiance");
    const auto xb = gpu.upload(ax.data(), ax.size() * 4, BufferUsage::Storage, "glaze aux");
    const auto nb = gpu.upload(f.normals.data(), f.normals.size() * 4, BufferUsage::Storage, "glaze normals");
    const auto o8 = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "glaze packed");
    const Access R = Access::StorageRead;
    return finish(dispatchAndRead(gpu, passIndex("style_glaze"), {ub, pb, ab, lb, xb, nb, o8}, {Access::Uniform, R, R, R, R, R, Access::StorageWrite}, {o8}, n, {&packed}),
                  gpu, "glaze", err);
}

bool runHatch(rhi::Device& dev, const style::HatchPlan& p, int w, int h, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(w * h);
    HatchParamsGpu P{std::uint32_t(w), std::uint32_t(h), std::uint32_t(p.layers), 0, {}, {}, {}};
    put4(P.a, p.freq, p.width, p.umb, p.contour); put3(P.ink, p.ink); put3(P.paper, p.paper);
    const auto ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "hatch params");
    const auto o8 = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "hatch packed");
    return finish(dispatchAndRead(gpu, passIndex("style_hatch"), {ub, o8}, {Access::Uniform, Access::StorageWrite}, {o8}, n, {&packed}), gpu, "hatch", err);
}

}  // namespace raw::gpu_check::style_gpu
