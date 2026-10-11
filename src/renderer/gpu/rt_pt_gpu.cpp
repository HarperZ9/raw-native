// The path tracer on the GPU from the host: see raw/renderer/rt_pt_gpu.hpp.
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "shaders/pt_layout.hpp"
#include "rt_dev.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
using namespace rtdev;
struct Packed { std::vector<std::int32_t> ni; std::vector<float> nb, tris, attrs, mats; std::vector<std::uint32_t> tex; std::uint32_t emitterOffset{0}; };
Packed pack(const rt::PtScene& s) {
    Packed p;
    for (const rt::Node& x : s.tree.nodes) {
        p.ni.insert(p.ni.end(), {x.left, x.right, x.tri, 0});
        p.nb.insert(p.nb.end(), {x.lo[0], x.lo[1], x.lo[2], x.hi[0], x.hi[1], x.hi[2], 0.0f, 0.0f});
    }
    const swr::Geometry& g = s.geo;
    for (const Tri& t : rt::trianglesOf(g)) for (Vec3 v : {t.a, t.b, t.c}) p.tris.insert(p.tris.end(), {v.x, v.y, v.z});
    p.attrs.assign(g.triangles() * 16, 0.0f);
    for (std::size_t k = 0; k < g.triangles(); ++k) {
        for (std::size_t v = 0; v < 3; ++v) {
            const std::uint32_t i = g.idx[k * 3 + v];
            float* a = &p.attrs[k * 16 + v * 5];
            a[0] = g.uv[i].x; a[1] = g.uv[i].y; a[2] = g.nrm[i].x; a[3] = g.nrm[i].y; a[4] = g.nrm[i].z;
        }
        p.attrs[k * 16 + 15] = float(g.triTexture[k]);
    }
    for (const auto& e : s.tex.entries) p.tex.insert(p.tex.end(), {std::uint32_t(3 * s.tex.entries.size()) + e.offset, e.width, e.height});
    p.tex.insert(p.tex.end(), s.tex.texels.begin(), s.tex.texels.end());
    for (const rt::PtMaterial& m : s.materials)
        p.mats.insert(p.mats.end(), {m.baseFactor, m.roughness, m.metallic, m.specular, m.emission.x, m.emission.y, m.emission.z, 0.0f});
    p.emitterOffset = std::uint32_t(p.mats.size());
    for (std::size_t k = 0; k < s.emitters.size(); ++k) { p.mats.push_back(float(s.emitters[k])); p.mats.push_back(s.emitterCdf[k]); }
    return p;
}
void putCam(float* w, const rt::PtCamera& c, int width, int height) {
    const Vec3 f = normalize(c.target - c.eye), s = normalize(cross(f, c.up)), u = cross(s, f);
    const float v[16] = {c.eye.x, c.eye.y, c.eye.z, std::tan(c.fovy * 0.5f), f.x, f.y, f.z, float(width) / float(height),
                         s.x, s.y, s.z, 0.0f, u.x, u.y, u.z, 0.0f};
    std::memcpy(w, v, sizeof v);
}
// The 56 uniform words of PtParams.
std::vector<std::uint32_t> params(const rt::PtScene& s, const rt::PathTraceDesc& d, std::uint32_t begin, std::uint32_t count,
                                  bool first, std::uint32_t emitterOffset) {
    std::vector<std::uint32_t> w(56, 0u);
    std::uint32_t flags = (d.jitter ? 1u : 0u) | (d.russianRoulette ? 2u : 0u) | (d.controls.dropLambertCosine ? 4u : 0u) |
                          (d.controls.misWeightOne ? 8u : 0u) | (first ? 16u : 0u);
    const std::uint32_t head[11] = {std::uint32_t(d.width), std::uint32_t(d.height), begin, count, d.maxBounces, flags,
                                    std::uint32_t(d.seed), std::uint32_t(d.seed >> 32), std::uint32_t(s.tree.root),
                                    std::uint32_t(s.emitters.size()), emitterOffset};
    std::memcpy(w.data(), head, sizeof head);
    float f[45] = {};
    f[0] = s.emitterArea;
    putCam(f + 1, d.camera, d.width, d.height);
    putCam(f + 17, d.previous, d.width, d.height);
    const float sunOn = std::max(s.sunIrradiance.x, std::max(s.sunIrradiance.y, s.sunIrradiance.z)) > 0.0f ? 1.0f : 0.0f;
    const Vec3 sd = normalize(s.sunDir);
    const float tail[12] = {sd.x, sd.y, sd.z, sunOn, s.sunIrradiance.x, s.sunIrradiance.y, s.sunIrradiance.z, 0.0f, s.sky.x, s.sky.y, s.sky.z, 0.0f};
    std::memcpy(f + 33, tail, sizeof tail);
    std::memcpy(w.data() + 11, f, sizeof f);
    return w;
}
}  // namespace

rt::PathTraceOutput pathTraceGpu(rhi::Device& device, const rt::PtScene& s, const rt::PathTraceDesc& d, std::uint32_t slice) {
    rt::PathTraceOutput O;
    O.width = d.width; O.height = d.height; O.spp = d.spp;
    Dev D{device, O.error, passTable(pt_passes::kPasses)};
    slice = std::max<std::uint32_t>(1, slice);
    const Packed p = pack(s);
    const std::uint64_t npx = std::uint64_t(d.width) * std::uint64_t(d.height);
    if (!(D.cl = device.begin(O.error))) return O;
    const BufferHandle NI = D.upload(p.ni.data(), p.ni.size() * 4, "pt ni"), NB = D.upload(p.nb.data(), p.nb.size() * 4, "pt nb");
    const BufferHandle T = D.upload(p.tris.data(), p.tris.size() * 4, "pt tris"), A = D.upload(p.attrs.data(), p.attrs.size() * 4, "pt attrs");
    const BufferHandle TX = D.upload(p.tex.data(), p.tex.size() * 4, "pt tex"), M = D.upload(p.mats.data(), p.mats.size() * 4, "pt mats");
    const BufferHandle ACC = D.make(npx * 64, kRW, "pt acc"), OUT = D.make(npx * 64, kRW, "pt out"), RB = D.make(npx * 64, kRB, "pt rb");
    const std::uint32_t gx = std::uint32_t((d.width + 7) / 8), gy = std::uint32_t((d.height + 7) / 8);
    std::uint32_t b = 0;
    // One submission a slice, so no single submission runs long enough for the OS GPU watchdog
    // (Windows TDR, 2 s by default): the first RTX run at 1280 x 720, 1024 spp recorded every
    // slice into one submission and failed (evidence/rt-r2-runs.json, run 15).
    do {
        if (b > 0 && !(D.cl = device.begin(O.error))) return O;
        const std::uint32_t n = std::min(slice, d.spp - b);
        const std::vector<std::uint32_t> w = params(s, d, d.sppBegin + b, n, b == 0, p.emitterOffset);
        D.run("pt_trace", {D.uniform(w.data(), w.size()), NI, NB, T, A, TX, M, ACC}, gx, gy);
        b += n;
        if (!device.submitAndWait(O.error)) return O;
    } while (b < d.spp);
    if (!(D.cl = device.begin(O.error))) return O;
    const std::vector<std::uint32_t> w = params(s, d, d.sppBegin, d.spp, false, p.emitterOffset);
    D.run("pt_finish", {D.uniform(w.data(), w.size()), ACC, OUT}, gx, gy);
    D.to({OUT}, Access::CopySrc);
    D.cl->copyBuffer(OUT, 0, RB, 0, npx * 64);
    if (!device.submitAndWait(O.error)) return O;
    std::vector<float> o;
    if (!D.read(RB, std::size_t(npx) * 16, o)) return O;
    O.radiance.resize(npx * 3); O.albedo.resize(npx * 3); O.normal.resize(npx * 3);
    O.depth.resize(npx); O.variance.resize(npx); O.motion.resize(npx * 2); O.triangle.resize(npx);
    for (std::size_t q = 0; q < npx; ++q) {
        const float* x = &o[q * 16];
        for (int k = 0; k < 3; ++k) { O.radiance[q * 3 + k] = x[k]; O.albedo[q * 3 + k] = x[3 + k]; O.normal[q * 3 + k] = x[6 + k]; }
        O.depth[q] = x[9]; O.motion[q * 2] = x[10]; O.motion[q * 2 + 1] = x[11]; O.variance[q] = x[12]; O.triangle[q] = int(x[13]);
    }
    return O;
}

}  // namespace raw::gpu_check
