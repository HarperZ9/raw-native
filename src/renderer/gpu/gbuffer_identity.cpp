// The hardware G-buffer against the CPU rasterizer: see raw/renderer/raster_identity.hpp.
#include "raw/renderer/raster_identity.hpp"
#include "raw/renderer/raster.hpp"
#include "frame_pack.hpp"
#include "gpu_util.hpp"
#include "shaders/raster_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace raw::gpu_check {
namespace {
using namespace util;
using rhi::TextureFormat;
using rhi::TextureUsage;
struct V2 { double x, y; };
// Screen-space corners of triangle t, as src/renderer/raster.cpp maps them.
bool screenTri(const std::vector<float>& tris, std::size_t t, const Mat4& vp, int w, int h, int bits, V2 out[3]) {
    for (int k = 0; k < 3; ++k) {
        const float* p = &tris[t * 24 + std::size_t(k) * 3];
        const Vec4 c = mul(vp, Vec4{p[0], p[1], p[2], 1.0f});
        if (c.w <= 1e-6f) return false;
        // As raster.cpp computes them in float, then snapped as the reference rasterized them.
        const float invw = 1.0f / c.w, sx = (c.x * invw * 0.5f + 0.5f) * float(w), sy = (1.0f - (c.y * invw * 0.5f + 0.5f)) * float(h);
        const double q = bits > 0 ? double(1 << bits) : 0.0;
        out[k] = q > 0 ? V2{std::nearbyint(sx * q) / q, std::nearbyint(sy * q) / q} : V2{sx, sy};
    }
    return true;
}
double edgeDistance(const V2 v[3], double px, double py) {
    double best = 1e30;
    for (int k = 0; k < 3; ++k) {
        const V2 a = v[k], b = v[(k + 1) % 3];
        const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        const double t = l2 > 0 ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, std::hypot(px - (a.x + t * dx), py - (a.y + t * dy)));
    }
    return best;
}
}  // namespace

GbufferCase gbufferCase(rhi::Device& dev, const std::string& name, const Scene& scene, int size, bool moveOne, int subpixelBits, std::string& err) {
    GbufferCase c; c.scene = name; c.width = c.height = size;
    const uint32_t W = uint32_t(size), H = uint32_t(size);
    Buffer<uint32_t> cpuIds;
    RasterOptions ro; ro.perspectiveDepth = true; ro.triangleIds = &cpuIds; ro.subpixelBits = subpixelBits;
    const GBuffer g = rasterize(scene, size, size, nullptr, nullptr, ro);
    const std::vector<float> orig = gpu_host::packTriangles(scene);
    std::vector<float> tris = orig;
    const Mat4 vp = mul(scene.camera.proj(), scene.camera.view());
    if (moveOne) {                                  // the control: the triangle covering most pixels, moved 1% of the scene
        std::vector<int> count(tris.size() / 24 + 1, 0);
        for (uint32_t id : cpuIds.px) if (id) ++count[id - 1];
        const std::size_t t = std::size_t(std::max_element(count.begin(), count.end()) - count.begin());
        float lo = 1e30f, hi = -1e30f;
        for (std::size_t i = 0; i < tris.size(); i += 24) for (int k = 0; k < 3; ++k) { lo = std::min(lo, tris[i + std::size_t(k) * 3]); hi = std::max(hi, tris[i + std::size_t(k) * 3]); }
        for (int k = 0; k < 3; ++k) tris[t * 24 + std::size_t(k) * 3] += 0.01f * (hi - lo);
    }
    Gpu gpu(dev);
    rhi::RasterPipelineDesc pd;
    pd.targets[0] = TextureFormat::RGBA32Float; pd.targets[1] = TextureFormat::RGBA32Float;
    pd.targets[2] = TextureFormat::RGBA8Unorm; pd.targets[3] = TextureFormat::RGBA32Float;
    pd.targetCount = 4; pd.depth.enabled = true;
    const rhi::RasterPipelineHandle pipe = raster(dev, raster_passes::kRasters, "gbuffer", pd, gpu.err);
    const rhi::BufferHandle ub = gpu.upload(vp.m, sizeof vp.m, rhi::BufferUsage::Uniform, "camera");
    const rhi::BufferHandle tb = gpu.upload(tris.data(), tris.size() * 4, rhi::BufferUsage::Storage, "triangles");
    const auto rt = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    const rhi::TextureHandle tex[4] = {gpu.texture(W, H, pd.targets[0], rt, "gb position"), gpu.texture(W, H, pd.targets[1], rt, "gb normal"),
                                       gpu.texture(W, H, pd.targets[2], rt, "gb albedo"), gpu.texture(W, H, pd.targets[3], rt, "gb id")};
    const rhi::TextureHandle depth = gpu.texture(W, H, TextureFormat::Depth32Float, TextureUsage::RenderTarget, "gb depth");
    std::vector<std::vector<uint8_t>> out;
    const bool ok = pipe.valid() && gpu.run([&](rhi::CommandList& cl) {
        const rhi::BufferBarrier b[2] = {{ub, rhi::Access::CopyDst, rhi::Access::Uniform}, {tb, rhi::Access::CopyDst, rhi::Access::StorageRead}};
        cl.barrier(b);
        rhi::RenderPassDesc rp; rp.colorCount = 4; rp.depth = depth;
        for (int k = 0; k < 4; ++k) rp.color[k].texture = tex[k];
        cl.beginRenderPass(rp);
        const rhi::RasterBind binds[2] = {{ub, {}, {}}, {tb, {}, {}}};
        cl.draw(pipe, binds, uint32_t(tris.size() / 24 * 3));
        cl.endRenderPass();
    }, {{tex[0], pd.targets[0]}, {tex[1], pd.targets[1]}, {tex[2], pd.targets[2]}, {tex[3], pd.targets[3]}}, out, W, H);
    if (!ok) { err = gpu.err; return c; }
    const uint32_t p32 = rhi::textureRowPitch(W, TextureFormat::RGBA32Float), p8 = rhi::textureRowPitch(W, TextureFormat::RGBA8Unorm);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        float pos[4], nrm[4], id[4];
        std::memcpy(pos, out[0].data() + std::size_t(y) * p32 + std::size_t(x) * 16, 16);
        std::memcpy(nrm, out[1].data() + std::size_t(y) * p32 + std::size_t(x) * 16, 16);
        std::memcpy(id, out[3].data() + std::size_t(y) * p32 + std::size_t(x) * 16, 16);
        const uint8_t* alb = out[2].data() + std::size_t(y) * p8 + std::size_t(x) * 4;
        const uint32_t gt = uint32_t(std::lround(id[0])), ct = cpuIds.at(x, y);
        if (gt == 0 && ct == 0) continue;
        if (gt == ct) {
            ++c.both;
            const Vec3 cp = g.position.at(x, y), cn = g.normal.at(x, y), ca = g.albedo.at(x, y);
            const double dp = std::sqrt(double(pos[0] - cp.x) * (pos[0] - cp.x) + double(pos[1] - cp.y) * (pos[1] - cp.y) + double(pos[2] - cp.z) * (pos[2] - cp.z));
            c.worstPosition = std::max(c.worstPosition, dp / (1e-3 * (1.0 + std::sqrt(double(cp.x) * cp.x + double(cp.y) * cp.y + double(cp.z) * cp.z))));
            const double dot = double(nrm[0]) * cn.x + double(nrm[1]) * cn.y + double(nrm[2]) * cn.z;
            c.worstNormal = std::max(c.worstNormal, (1.0 - dot) / 1e-5);
            if (std::getenv("RAW_NATIVE_RASTER_DUMP") && ((1.0 - dot) / 1e-5 > 1.0 || (x == size / 2 && y == size / 2)))   // diagnosis
                std::fprintf(stderr, "px %d %d tri %u gpu n %.5f %.5f %.5f cpu n %.5f %.5f %.5f gpu alb %d %d %d cpu alb %.4f %.4f %.4f\n", x, y, gt,
                             nrm[0], nrm[1], nrm[2], cn.x, cn.y, cn.z, alb[0], alb[1], alb[2], ca.x, ca.y, ca.z);
            c.worstDistance = std::max(c.worstDistance, std::fabs(double(id[1]) - g.depth.at(x, y)) / g.depth.at(x, y) / 1e-4);
            const float want[3] = {ca.x, ca.y, ca.z};
            // The APIs' float-to-unorm conversion rounds to nearest, ties to even (0.3 * 255 = 76.5 gives 76).
            for (int k = 0; k < 3; ++k) if (alb[k] != std::nearbyint(std::clamp(want[k], 0.0f, 1.0f) * 255.0f)) { ++c.albedoDiffs; break; }
            continue;
        }
        ++c.coverageOrIdDiffs;
        bool explained = false;
        double nearest = 1e30;
        for (uint32_t t : {gt, ct}) {
            V2 v[3];
            if (t && screenTri(t == gt ? tris : orig, t - 1, vp, size, size, subpixelBits, v)) nearest = std::min(nearest, edgeDistance(v, x + 0.5, y + 0.5));
        }
        explained = nearest <= 0.01;
        const double tie = gt && ct ? std::fabs(double(id[1]) - g.depth.at(x, y)) / g.depth.at(x, y) : -1.0;
        if (!explained && tie >= 0.0 && tie <= 1e-4) explained = true;
        if (explained) ++c.diffsExplained;
        else if (c.unexplained.size() < 8) {
            char b[200];
            std::snprintf(b, sizeof b, "{\"x\": %d, \"y\": %d, \"gpu_tri\": %u, \"cpu_tri\": %u, \"edge_px\": %.4f, \"depth_rel\": %.3e}", x, y, gt, ct, nearest, tie);
            c.unexplained.push_back(b);
        }
    }
    return c;
}
}  // namespace raw::gpu_check
