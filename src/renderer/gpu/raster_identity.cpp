// RHI version 3 checks: see raw/renderer/raster_identity.hpp.
#include "raw/renderer/raster_identity.hpp"
#include "raw/renderer/raster.hpp"
#include "frame_pack.hpp"
#include "gpu_util.hpp"
#include "shaders/raster_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::gpu_check {
namespace {
using namespace util;
using rhi::TextureFormat;
using rhi::TextureUsage;
constexpr uint32_t FW = 64, FH = 48;
const auto& kLayouts = raster_passes::kRasters;

FormatCase formatCase(rhi::Device& dev, TextureFormat f, const char* name, std::string& err) {
    FormatCase c; c.format = name;
    Gpu g(dev);
    rhi::RasterPipelineDesc pd; pd.targets[0] = f;
    const rhi::RasterPipelineHandle pipe = raster(dev, kLayouts, "fmt_pattern", pd, g.err);
    const float scale[4] = {f == TextureFormat::RGBA8Unorm ? 1.0f / 255.0f : 1.0f / 256.0f, 0, 0, 0};
    const rhi::BufferHandle ub = g.upload(scale, sizeof scale, rhi::BufferUsage::Uniform, "pattern scale");
    const rhi::TextureHandle t = g.texture(FW, FH, f, TextureUsage::RenderTarget | TextureUsage::CopySrc, name);
    std::vector<std::vector<uint8_t>> out;
    if (!pipe.valid() || !g.run([&](rhi::CommandList& cl) {
            const rhi::BufferBarrier b[1] = {{ub, rhi::Access::CopyDst, rhi::Access::Uniform}};
            cl.barrier(b);
            rhi::RenderPassDesc rp; rp.color[0].texture = t;
            cl.beginRenderPass(rp);
            const rhi::RasterBind binds[1] = {{ub, {}, {}}};
            cl.draw(pipe, binds, 3);
            cl.endRenderPass();
        }, {{t, f}}, out, FW, FH)) { err = g.err; return c; }
    const uint32_t pitch = rhi::textureRowPitch(FW, f), channels = f == TextureFormat::R32Float ? 1 : 4;
    for (uint32_t y = 0; y < FH; ++y) for (uint32_t x = 0; x < FW; ++x) for (uint32_t k = 0; k < channels; ++k) {
        const uint32_t n = (7 * x + 13 * y + 5 * k) & 255;
        const uint8_t* p = out[0].data() + y * pitch;
        bool ok;
        if (f == TextureFormat::RGBA8Unorm) ok = p[x * 4 + k] == n;
        else if (f == TextureFormat::RGBA16Float) { uint16_t h; std::memcpy(&h, p + (x * 4 + k) * 2, 2); ok = halfToFloat(h) == float(n) / 256.0f; }
        else { float v; std::memcpy(&v, p + (x * channels + k) * 4, 4); ok = v == float(n) / 256.0f; }
        ++c.texels; if (!ok) ++c.mismatches;
    }
    return c;
}

// The rasterizer's sub-pixel precision, measured: a triangle whose left edge sits at x = 10.52 px
// covers the centre of pixel 10 (x = 10.5) only if the edge snaps to 10.5, which 4 bits of
// precision do (168.32 / 16 rounds to 168) and 8 bits do not (2693 / 256 = 10.5195). The
// left edge is inside under the top-left rule. Returns 4 or 8, or 0 on error.
int probeBits(rhi::Device& dev, std::string& err) {
    const float W = 32, H = 8, xs[3] = {10.52f, 30.0f, 10.52f}, ys[3] = {-1.0f, -1.0f, 20.0f};
    std::vector<float> tri;
    for (int k = 0; k < 3; ++k) { tri.push_back(xs[k] / W * 2.0f - 1.0f); tri.push_back(1.0f - ys[k] / H * 2.0f); tri.push_back(0.5f); }
    tri.insert(tri.end(), {1.0f, 1.0f, 1.0f});
    Gpu g(dev);
    const rhi::RasterPipelineHandle pipe = raster(dev, kLayouts, "depth_tris", rhi::RasterPipelineDesc{}, g.err);
    const rhi::BufferHandle tb = g.upload(tri.data(), tri.size() * 4, rhi::BufferUsage::Storage, "probe triangle");
    const rhi::TextureHandle ct = g.texture(32, 8, TextureFormat::RGBA8Unorm, TextureUsage::RenderTarget | TextureUsage::CopySrc, "probe");
    std::vector<std::vector<uint8_t>> out;
    const bool ok = pipe.valid() && g.run([&](rhi::CommandList& cl) {
        const rhi::BufferBarrier b[1] = {{tb, rhi::Access::CopyDst, rhi::Access::StorageRead}};
        cl.barrier(b);
        rhi::RenderPassDesc rp; rp.color[0].texture = ct;
        cl.beginRenderPass(rp);
        const rhi::RasterBind binds[1] = {{tb, {}, {}}};
        cl.draw(pipe, binds, 3);
        cl.endRenderPass();
    }, {{ct, TextureFormat::RGBA8Unorm}}, out, 32, 8);
    if (!ok) { err = g.err; return 0; }
    return out[0][2 * rhi::textureRowPitch(32) + 10 * 4] > 127 ? 4 : 8;
}

// Draw 64 overlapping triangles in the given order; returns the RGBA8 image.
bool depthImage(rhi::Device& dev, bool depth, bool reverse, std::vector<uint8_t>& img, std::string& err) {
    std::vector<float> tris;
    uint64_t s = 0x5EEDull;
    const auto rnd = [&] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) / 9007199254740992.0; };
    for (int k = 0; k < 64; ++k) {
        const double cx = rnd() * 1.6 - 0.8, cy = rnd() * 1.6 - 0.8, r = 0.3 + 0.5 * rnd(), z = (k * 37 % 64 + 1) / 66.0;
        for (int v = 0; v < 3; ++v) {
            const double a = 2.0 * 3.14159265358979 * (v / 3.0 + rnd() * 0.1);
            tris.push_back(float(cx + r * std::cos(a))); tris.push_back(float(cy + r * std::sin(a))); tris.push_back(float(z));
        }
        for (int c = 0; c < 3; ++c) tris.push_back(float(int(rnd() * 255.0)) / 255.0f);
    }
    if (reverse) {
        std::vector<float> r;
        for (int k = 63; k >= 0; --k) r.insert(r.end(), tris.begin() + k * 12, tris.begin() + k * 12 + 12);
        tris.swap(r);
    }
    Gpu g(dev);
    rhi::RasterPipelineDesc pd; pd.depth.enabled = depth;
    const rhi::RasterPipelineHandle pipe = raster(dev, kLayouts, "depth_tris", pd, g.err);
    const rhi::BufferHandle tb = g.upload(tris.data(), tris.size() * 4, rhi::BufferUsage::Storage, "triangles");
    const rhi::TextureHandle ct = g.texture(128, 128, TextureFormat::RGBA8Unorm, TextureUsage::RenderTarget | TextureUsage::CopySrc, "depth colour");
    const rhi::TextureHandle dt = depth ? g.texture(128, 128, TextureFormat::Depth32Float, TextureUsage::RenderTarget, "depth") : rhi::TextureHandle{};
    std::vector<std::vector<uint8_t>> out;
    const bool ok = pipe.valid() && g.run([&](rhi::CommandList& cl) {
        const rhi::BufferBarrier b[1] = {{tb, rhi::Access::CopyDst, rhi::Access::StorageRead}};
        cl.barrier(b);
        rhi::RenderPassDesc rp; rp.color[0].texture = ct; rp.depth = dt;
        cl.beginRenderPass(rp);
        const rhi::RasterBind binds[1] = {{tb, {}, {}}};
        cl.draw(pipe, binds, 64 * 3);
        cl.endRenderPass();
    }, {{ct, TextureFormat::RGBA8Unorm}}, out, 128, 128);
    if (!ok) { err = g.err; return false; }
    img = out[0];
    return true;
}
}  // namespace

int probeSubpixelBits(rhi::Device& dev, std::string& err) { return probeBits(dev, err); }

std::string joined(const std::vector<std::string>& v) { std::string s; for (std::size_t k = 0; k < v.size(); ++k) s += (k ? ", " : "") + v[k]; return s; }

bool RasterIdentity::pass() const {
    if (!error.empty() || formats.empty() || scenes.empty() || !depthOrderEqual || !depthControlDiffers || !controlFails) return false;
    for (const auto& f : formats) if (f.mismatches) return false;
    for (const auto& s : scenes) if (!s.pass()) return false;
    return true;
}

GbufferCase gbufferCase(rhi::Device& dev, const std::string& name, const Scene& scene, int size, bool moveOne, int subpixelBits, std::string& err);

RasterIdentity rasterIdentity(rhi::Device& dev, const std::vector<std::pair<std::string, const Scene*>>& scenes, int size, int subpixelBits) {
    RasterIdentity r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    const std::pair<TextureFormat, const char*> fmts[4] = {{TextureFormat::RGBA8Unorm, "rgba8unorm"}, {TextureFormat::RGBA16Float, "rgba16float"},
                                                           {TextureFormat::RGBA32Float, "rgba32float"}, {TextureFormat::R32Float, "r32float"}};
    for (const auto& f : fmts) { r.formats.push_back(formatCase(dev, f.first, f.second, r.error)); if (!r.error.empty()) return r; }
    std::vector<uint8_t> a, b, c, d;
    if (!depthImage(dev, true, false, a, r.error) || !depthImage(dev, true, true, b, r.error) ||
        !depthImage(dev, false, false, c, r.error) || !depthImage(dev, false, true, d, r.error)) return r;
    r.depthOrderEqual = a == b;
    r.depthControlDiffers = c != d;
    r.subpixelBits = subpixelBits > 0 ? subpixelBits : probeSubpixelBits(dev, r.error);
    if (!r.error.empty()) return r;
    subpixelBits = r.subpixelBits;
    for (const auto& s : scenes) {
        r.scenes.push_back(gbufferCase(dev, s.first, *s.second, size, false, subpixelBits, r.error));
        if (!r.error.empty()) return r;
    }
    if (!scenes.empty()) {
        const GbufferCase ctl = gbufferCase(dev, scenes.front().first + " (control)", *scenes.front().second, size, true, subpixelBits, r.error);
        r.controlFails = !ctl.pass();
    }
    return r;
}

std::string RasterIdentity::json() const {
    std::string e;                                   // the error as a JSON string body
    for (char ch : error) { if (ch == '"' || ch == '\\') e += '\\'; e += ch == '\n' ? ' ' : ch; }
    std::string s = "{\n \"check\": \"m3 rhi3 raster identity\",\n \"backend\": \"" + backend + "\",\n \"adapter\": \"" + adapter + "\",\n \"error\": \"" + e + "\",\n \"formats\": [";
    for (std::size_t k = 0; k < formats.size(); ++k)
        s += (k ? ", " : "") + std::string("{\"format\": \"") + formats[k].format + "\", \"texels\": " + std::to_string(formats[k].texels) + ", \"mismatches\": " + std::to_string(formats[k].mismatches) + "}";
    s += "],\n \"depth_order_equal\": " + std::string(depthOrderEqual ? "true" : "false") + ",\n \"depth_control_differs\": " + (depthControlDiffers ? "true" : "false") +
         ",\n \"subpixel_bits\": " + std::to_string(subpixelBits) + ",\n \"gbuffer\": [\n";
    for (std::size_t k = 0; k < scenes.size(); ++k) {
        const GbufferCase& c = scenes[k];
        char b[2048];
        std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"size\": %d, \"same_triangle_pixels\": %d, \"differing_pixels\": %d, \"explained_by_edge_or_tie\": %d,"
                      " \"position_ratio\": %.4f, \"normal_ratio\": %.4f, \"distance_ratio\": %.4f, \"albedo_diffs\": %d, \"pass\": %s, \"unexplained\": [%s]}%s\n",
                      c.scene.c_str(), c.width, c.both, c.coverageOrIdDiffs, c.diffsExplained, c.worstPosition, c.worstNormal, c.worstDistance,
                      c.albedoDiffs, c.pass() ? "true" : "false", joined(c.unexplained).c_str(), k + 1 < scenes.size() ? "," : "");
        s += b;
    }
    return s + " ],\n \"control_moved_triangle_fails\": " + (controlFails ? "true" : "false") + ",\n \"pass\": " + (pass() ? "true" : "false") + "\n}\n";
}
}  // namespace raw::gpu_check
