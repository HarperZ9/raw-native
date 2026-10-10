// The ray marcher on the GPU from the host: see raw/renderer/sdf_gpu.hpp.
#include "raw/renderer/sdf_gpu.hpp"
#include "shaders/sdf_layout.hpp"
#include "rt_dev.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
using namespace rtdev;
// The 40 words of SdfParams.
std::vector<std::uint32_t> params(const sdf::Scene& s, const SdfJob& j, std::uint32_t count) {
    std::vector<std::uint32_t> w(40, 0u);
    w[0] = std::uint32_t(j.w); w[1] = std::uint32_t(j.h); w[2] = std::uint32_t(s.prog.nodes.size() / sdf::kNodeFloats);
    w[3] = j.flags; w[4] = count;
    const Vec3 f = normalize(s.target - s.eye), sd = normalize(cross(f, s.up)), u = cross(sd, f), l = normalize(s.sunDir);
    const float v[32] = {s.tMax, s.prog.fractal ? 0.6f : 1.0f, j.softK, s.fogA, s.fogB, s.fogMax, j.nearZ, j.farZ,
                         s.eye.x, s.eye.y, s.eye.z, std::tan(s.fovy * 0.5f), f.x, f.y, f.z, float(j.w) / float(j.h),
                         sd.x, sd.y, sd.z, 0.0f, u.x, u.y, u.z, 0.0f, l.x, l.y, l.z, 0.0f, 0, 0, 0, 0};
    std::memcpy(&w[8], v, sizeof v);
    return w;
}
}  // namespace

SdfGpu sdfGpu(rhi::Device& device, const sdf::Scene& s, const SdfJob& j) {
    SdfGpu G;
    Dev D{device, G.error, passTable(sdf_passes::kPasses)};
    const std::uint64_t npx = std::uint64_t(j.w) * std::uint64_t(j.h);
    if (!(D.cl = device.begin(G.error))) return G;
    const BufferHandle N = D.upload(s.prog.nodes.data(), s.prog.nodes.size() * 4, "sdf program");
    const BufferHandle M = D.make(npx * 32, kRW, "sdf march"), RM = D.make(npx * 32, kRB, "rb march");
    const std::uint32_t gx = std::uint32_t((j.w + 7) / 8), gy = std::uint32_t((j.h + 7) / 8);
    const std::vector<std::uint32_t> p0 = params(s, j, 0);
    D.run("sdf_march", {D.uniform(p0.data(), p0.size()), N, M}, gx, gy);
    BufferHandle GO, RG, TO, RT, FO, RF, CO, RC;
    if (j.god) {
        GO = D.make(npx * 16, kRW, "sdf god"); RG = D.make(npx * 16, kRB, "rb god");
        D.run("sdf_god", {D.uniform(p0.data(), p0.size()), N, M, GO}, gx, gy);
    }
    const std::uint32_t nPts = std::uint32_t(j.points.size() / 6), nFog = std::uint32_t(j.fogCases.size() / 8);
    if (nPts) {
        const std::vector<std::uint32_t> pp = params(s, j, nPts);
        const BufferHandle X = D.upload(j.points.data(), j.points.size() * 4, "sdf points");
        TO = D.make(std::uint64_t(nPts) * 8, kRW, "sdf terms"); RT = D.make(std::uint64_t(nPts) * 8, kRB, "rb terms");
        D.run("sdf_terms", {D.uniform(pp.data(), pp.size()), N, X, TO}, (nPts + 63) / 64);
    }
    if (nFog) {
        const std::vector<std::uint32_t> pf = params(s, j, nFog);
        const BufferHandle C = D.upload(j.fogCases.data(), j.fogCases.size() * 4, "sdf fog cases");
        FO = D.make(std::uint64_t(nFog) * 16, kRW, "sdf fog"); RF = D.make(std::uint64_t(nFog) * 16, kRB, "rb fog");
        D.run("sdf_fog", {D.uniform(pf.data(), pf.size()), N, C, FO}, (nFog + 63) / 64);
    }
    if (j.rasterVis && j.rasterDepth) {
        const BufferHandle B = D.upload(j.rasterVis->data(), j.rasterVis->size() * 4, "sdf raster vis");
        const BufferHandle Z = D.upload(j.rasterDepth->data(), j.rasterDepth->size() * 4, "sdf raster depth");
        CO = D.make(npx * 16, kRW, "sdf composite"); RC = D.make(npx * 16, kRB, "rb composite");
        D.run("sdf_composite", {D.uniform(p0.data(), p0.size()), N, B, Z, M, CO}, gx, gy);
    }
    D.to({M}, Access::CopySrc);
    D.cl->copyBuffer(M, 0, RM, 0, npx * 32);
    if (j.god) { D.to({GO}, Access::CopySrc); D.cl->copyBuffer(GO, 0, RG, 0, npx * 16); }
    if (nPts) { D.to({TO}, Access::CopySrc); D.cl->copyBuffer(TO, 0, RT, 0, std::uint64_t(nPts) * 8); }
    if (nFog) { D.to({FO}, Access::CopySrc); D.cl->copyBuffer(FO, 0, RF, 0, std::uint64_t(nFog) * 16); }
    if (j.rasterVis && j.rasterDepth) { D.to({CO}, Access::CopySrc); D.cl->copyBuffer(CO, 0, RC, 0, npx * 16); }
    if (!device.submitAndWait(G.error)) return G;
    D.read(RM, std::size_t(npx) * 8, G.march);
    if (j.god) D.read(RG, std::size_t(npx) * 4, G.god);
    if (nPts) D.read(RT, std::size_t(nPts) * 2, G.terms);
    if (nFog) D.read(RF, std::size_t(nFog) * 4, G.fog);
    if (j.rasterVis && j.rasterDepth) D.read(RC, std::size_t(npx) * 4, G.composite);
    return G;
}

}  // namespace raw::gpu_check
