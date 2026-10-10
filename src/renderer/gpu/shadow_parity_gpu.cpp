// The GPU side of the shadow parity: cascade maps from the shadow_depth raster pass and the
// shadow_lookup and shadow_contact compute passes. See raw/renderer/shadow_parity.hpp.
#include "shadow_parity_internal.hpp"
#include "gpu_util.hpp"
#include "shaders/raster_layout.hpp"
#include "shaders/shadow_layout.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check::shadow_gpu {
using namespace util;
using rhi::Access;
using rhi::BufferUsage;
using rhi::TextureFormat;
using rhi::TextureUsage;

bool maps(rhi::Device& dev, const std::vector<float>& tris, const shadows::CascadeSet& cs, std::array<GpuMap, shadows::kCascades>& out, std::string& err) {
    Gpu gpu(dev);
    const uint32_t S = uint32_t(cs.size);
    rhi::RasterPipelineDesc pd;
    pd.targets[0] = TextureFormat::RGBA32Float; pd.targetCount = 1; pd.depth.enabled = true;
    const rhi::RasterPipelineHandle pipe = raster(dev, raster_passes::kRasters, "shadow_depth", pd, gpu.err);
    const rhi::BufferHandle tb = gpu.upload(tris.data(), tris.size() * 4, BufferUsage::Storage, "triangles");
    rhi::BufferHandle ub[shadows::kCascades];
    rhi::TextureHandle col[shadows::kCascades], dep[shadows::kCascades];
    for (int k = 0; k < shadows::kCascades; ++k) {
        const std::array<float, 16> m = cs.matrix(k);
        ub[k] = gpu.upload(m.data(), sizeof(float) * 16, BufferUsage::Uniform, "cascade");
        col[k] = gpu.texture(S, S, TextureFormat::RGBA32Float, TextureUsage::RenderTarget | TextureUsage::CopySrc, "shadow map");
        dep[k] = gpu.texture(S, S, TextureFormat::Depth32Float, TextureUsage::RenderTarget, "shadow depth");
    }
    std::vector<std::vector<uint8_t>> rb;
    std::vector<std::pair<rhi::TextureHandle, TextureFormat>> reads;
    for (int k = 0; k < shadows::kCascades; ++k) reads.push_back({col[k], TextureFormat::RGBA32Float});
    const bool ok = pipe.valid() && gpu.run([&](rhi::CommandList& cl) {
        for (int k = 0; k < shadows::kCascades; ++k) {
            const rhi::BufferBarrier b[2] = {{ub[k], Access::CopyDst, Access::Uniform}, {tb, k ? Access::StorageRead : Access::CopyDst, Access::StorageRead}};
            cl.barrier(b);
            rhi::RenderPassDesc rp; rp.colorCount = 1; rp.depth = dep[k];
            rp.color[0].texture = col[k];
            rp.color[0].clear[1] = 1.0f;                  // depth 1 where nothing is drawn, as the CPU map
            cl.beginRenderPass(rp);
            const rhi::RasterBind binds[2] = {{ub[k], {}, {}}, {tb, {}, {}}};
            cl.draw(pipe, binds, uint32_t(tris.size() / 24 * 3));
            cl.endRenderPass();
        }
    }, reads, rb, S, S);
    if (!ok) { err = gpu.err; return false; }
    const uint32_t pitch = rhi::textureRowPitch(S, TextureFormat::RGBA32Float);
    for (int k = 0; k < shadows::kCascades; ++k) {
        GpuMap& g = out[std::size_t(k)];
        g.map.size = cs.size;
        g.map.depth.assign(std::size_t(S) * S, 1.0f);
        g.map.tri.assign(std::size_t(S) * S, 0u);
        for (uint32_t y = 0; y < S; ++y) for (uint32_t x = 0; x < S; ++x) {
            float v[4];
            std::memcpy(v, rb[std::size_t(k)].data() + std::size_t(y) * pitch + std::size_t(x) * 16, 16);
            g.map.tri[std::size_t(y) * S + x] = uint32_t(std::lround(v[0]));
            g.map.depth[std::size_t(y) * S + x] = v[1];
        }
    }
    return true;
}

namespace {
ShadowParamsGpu params(const shadows::CascadeSet& cs, uint32_t count, int w, int h, const Mat4& vp, const shadows::D3& toLight) {
    ShadowParamsGpu p{};
    const shadows::LookupParams lp;
    p.count = count; p.size = uint32_t(cs.size); p.width = uint32_t(w); p.height = uint32_t(h); p.steps = kContactSteps;
    const shadows::D3 basis[3] = {cs.right, cs.up, cs.light};
    for (int a = 0; a < 3; ++a) { p.basis[a][0] = float(basis[a].x); p.basis[a][1] = float(basis[a].y); p.basis[a][2] = float(basis[a].z); }
    for (int k = 0; k < shadows::kCascades; ++k) {
        const shadows::Cascade& c = cs.c[std::size_t(k)];
        p.box[k][0] = float(c.centre.x); p.box[k][1] = float(c.centre.y); p.box[k][2] = float(c.radius); p.box[k][3] = float(c.split);
        p.span[k][0] = float(c.zNear); p.span[k][1] = float(c.zFar);
    }
    p.cfg[0] = float(std::tan(lp.sunAngle)); p.cfg[1] = float(lp.normalOffset); p.cfg[2] = float(lp.depthBias); p.cfg[3] = float(kContactThickness);
    std::memcpy(p.vp, vp.m, 64);
    p.march[0] = float(toLight.x); p.march[1] = float(toLight.y); p.march[2] = float(toLight.z); p.march[3] = float(kContactLength);
    return p;
}
}  // namespace

bool lookups(rhi::Device& dev, const shadows::CascadeSet& cs, const std::array<GpuMap, shadows::kCascades>& m, const std::vector<float>& points,
             std::vector<float>& out, std::string& err) {
    Gpu gpu(dev);
    const auto& L = shadow_passes::kPasses[0];
    const rhi::PipelineHandle pipe = compute(dev, L.name, L.binds, L.bindCount, gpu.err);
    const uint32_t n = uint32_t(points.size() / 8);
    const ShadowParamsGpu p = params(cs, n, 0, 0, Mat4{}, {});
    std::vector<float> all;
    for (const GpuMap& g : m) all.insert(all.end(), g.map.depth.begin(), g.map.depth.end());
    const std::vector<float>& taps = shadows::poissonTaps();
    const rhi::BufferHandle ub = gpu.upload(&p, sizeof p, BufferUsage::Uniform, "shadow params");
    const rhi::BufferHandle mb = gpu.upload(all.data(), all.size() * 4, BufferUsage::Storage, "maps");
    const rhi::BufferHandle qb = gpu.upload(points.data(), points.size() * 4, BufferUsage::Storage, "points");
    const rhi::BufferHandle tb = gpu.upload(taps.data(), taps.size() * 4, BufferUsage::Storage, "taps");
    const rhi::BufferHandle ob = gpu.make(uint64_t(n) * 12, BufferUsage::Storage | BufferUsage::CopySrc, "visibility");
    out.assign(std::size_t(n) * 3, 0.0f);
    const bool ok = pipe.valid() && runBuffer(gpu, [&](rhi::CommandList& c) {
        const rhi::BufferBarrier in[5] = {{ub, Access::CopyDst, Access::Uniform}, {mb, Access::CopyDst, Access::StorageRead},
                                          {qb, Access::CopyDst, Access::StorageRead}, {tb, Access::CopyDst, Access::StorageRead},
                                          {ob, Access::Undefined, Access::StorageWrite}};
        c.barrier(in);
        const rhi::BufferHandle binds[5] = {ub, mb, qb, tb, ob};
        c.dispatch(pipe, binds, (n + 63) / 64, 1, 1);
    }, ob, uint64_t(n) * 12, out.data());
    if (!ok) err = gpu.err;
    return ok;
}

bool contact(rhi::Device& dev, const shadows::CascadeSet& cs, const std::vector<float>& dist, const std::vector<float>& points, int w, int h,
             const Mat4& vp, const shadows::D3& toLight, std::vector<float>& out, std::string& err) {
    Gpu gpu(dev);
    const auto& C = shadow_passes::kPasses[1];
    const rhi::PipelineHandle pipe = compute(dev, C.name, C.binds, C.bindCount, gpu.err);
    const uint32_t n = uint32_t(w * h);
    const ShadowParamsGpu p = params(cs, n, w, h, vp, toLight);
    const rhi::BufferHandle ub = gpu.upload(&p, sizeof p, BufferUsage::Uniform, "shadow params");
    const rhi::BufferHandle db = gpu.upload(dist.data(), dist.size() * 4, BufferUsage::Storage, "view depth");
    const rhi::BufferHandle qb = gpu.upload(points.data(), points.size() * 4, BufferUsage::Storage, "points");
    const rhi::BufferHandle ob = gpu.make(uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "contact");
    out.assign(n, 0.0f);
    const bool ok = pipe.valid() && runBuffer(gpu, [&](rhi::CommandList& c) {
        const rhi::BufferBarrier in[4] = {{ub, Access::CopyDst, Access::Uniform}, {db, Access::CopyDst, Access::StorageRead},
                                          {qb, Access::CopyDst, Access::StorageRead}, {ob, Access::Undefined, Access::StorageWrite}};
        c.barrier(in);
        const rhi::BufferHandle binds[4] = {ub, db, qb, ob};
        c.dispatch(pipe, binds, (n + 63) / 64, 1, 1);
    }, ob, uint64_t(n) * 4, out.data());
    if (!ok) err = gpu.err;
    return ok;
}

}  // namespace raw::gpu_check::shadow_gpu
