// The style passes' dispatch at large frames: two style.wgsl passes over w x h pixels through
// style_graph::Passes::add, read back in full. One 64-wide group per 64 pixels is 129,600 groups
// at 3840 x 2160, over the 65,535 a dimension that D3D12, Vulkan and WebGPU allow, so Passes::add
// folds the groups into two dimensions and every pass indexes gid.x + gid.y * 2,097,152.
#include "raw/renderer/style_parity.hpp"
#include "gpu_util.hpp"
#include "style_graph.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
namespace {
double decode(double v) {
    const double a = std::abs(v), l = a > 0.04045 ? std::pow((a + 0.055) / 1.055, 2.4) : a / 12.92;
    return v < 0 ? -l : l;
}
}  // namespace

StyleDispatchCheck styleDispatchCheck(rhi::Device& dev, int w, int h) {
    StyleDispatchCheck r;
    r.width = w; r.height = h;
    const std::uint32_t n = std::uint32_t(w) * std::uint32_t(h);
    r.groups = (n + 63) / 64;
    util::Gpu gpu(dev);
    const rhi::BufferUsage st = rhi::BufferUsage::Storage | rhi::BufferUsage::CopySrc | rhi::BufferUsage::CopyDst;
    const rhi::BufferUsage rd = rhi::BufferUsage::MapRead | rhi::BufferUsage::CopyDst;
    struct Pixel { std::uint32_t width, height, n, hasPrev; float rest[24]; } pp{std::uint32_t(w), std::uint32_t(h), 0, 0, {}};
    float post[20] = {float(w), float(h), 0, 0};
    // A ramp of display values that differs in every pixel's last bits, so a missed pixel shows.
    std::vector<float> src(std::size_t(n) * 4);
    for (std::uint32_t i = 0; i < n; ++i) { const float v = float(i % 4093) / 4092.0f; src[std::size_t(i) * 4] = v; src[std::size_t(i) * 4 + 1] = 1 - v; src[std::size_t(i) * 4 + 2] = 0.5f * v; src[std::size_t(i) * 4 + 3] = 1; }
    const std::vector<std::uint32_t> zeros(n, 0u);
    const rhi::BufferHandle pu = gpu.make(sizeof pp, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, "pixel params"), su = gpu.make(sizeof post, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, "post params");
    const rhi::BufferHandle ab = gpu.make(std::uint64_t(n) * 4, st, "clear target"), sb = gpu.make(std::uint64_t(n) * 16, st, "decode src"), pb = gpu.make(16, st, "decode prev (never read)");
    const rhi::BufferHandle db = gpu.make(std::uint64_t(n) * 16, st, "decode dst"), ar = gpu.make(std::uint64_t(n) * 4, rd, "clear readback"), dr = gpu.make(std::uint64_t(n) * 16, rd, "decode readback");
    if (!gpu.err.empty()) { r.error = gpu.err; return r; }
    graph::FrameGraph g(&dev);
    const graph::Resource PU = g.importBuffer("pixel params", pu), SU = g.importBuffer("post params", su), A = g.importBuffer("a", ab), S = g.importBuffer("src", sb);
    const graph::Resource PR = g.importBuffer("prev", pb), D = g.importBuffer("dst", db), AR = g.importBuffer("a readback", ar), DR = g.importBuffer("dst readback", dr);
    g.addPass("upload", {{PU, rhi::Access::CopyDst}, {SU, rhi::Access::CopyDst}, {A, rhi::Access::CopyDst}, {S, rhi::Access::CopyDst}}, [&](graph::PassContext& c) {
        c.cmd->upload(pu, 0, &pp, sizeof pp); c.cmd->upload(su, 0, post, sizeof post);
        c.cmd->upload(ab, 0, zeros.data(), zeros.size() * 4); c.cmd->upload(sb, 0, src.data(), src.size() * 4);
    });
    style_graph::Passes ps{dev, g, {}};
    if (!ps.add("style_pixel_clear", {PU, A}, n, r.error) || !ps.add("style_srgb_decode", {SU, S, PR, D}, n, r.error)) return r;
    g.addPass("readback", {{A, rhi::Access::CopySrc}, {D, rhi::Access::CopySrc}, {AR, rhi::Access::CopyDst}, {DR, rhi::Access::CopyDst}}, [&](graph::PassContext& c) {
        c.cmd->copyBuffer(ab, 0, ar, 0, std::uint64_t(n) * 4); c.cmd->copyBuffer(db, 0, dr, 0, std::uint64_t(n) * 16);
    });
    g.markOutput(AR); g.markOutput(DR);
    if (!g.execute(r.error)) return r;
    std::vector<std::uint32_t> a, d;
    if (!style_graph::readWords(dev, ar, n, a, r.error) || !style_graph::readWords(dev, dr, std::size_t(n) * 4, d, r.error)) return r;
    r.ran = true;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (a[i] != 0xffffffffu) { if (!r.clearMissed++) r.firstClearMiss = long(i); }
        bool ok = true;
        for (int c = 0; c < 3; ++c) {
            float got;
            std::memcpy(&got, &d[std::size_t(i) * 4 + std::size_t(c)], 4);
            ok = ok && std::abs(double(got) - decode(src[std::size_t(i) * 4 + std::size_t(c)])) <= 1e-5;
        }
        if (!ok) { if (!r.decodeMissed++) r.firstDecodeMiss = long(i); }
    }
    return r;
}

std::string StyleDispatchCheck::json() const {
    return "{\"check\": \"style passes over a large frame through Passes::add\", \"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) +
           ", \"groups\": " + std::to_string(groups) + ", \"ran\": " + (ran ? "true" : "false") + ", \"error\": \"" + error +
           "\", \"clear_missed\": " + std::to_string(clearMissed) + ", \"first_clear_miss\": " + std::to_string(firstClearMiss) +
           ", \"decode_missed\": " + std::to_string(decodeMissed) + ", \"first_decode_miss\": " + std::to_string(firstDecodeMiss) +
           ", \"pass\": " + (pass() ? "true" : "false") + "}\n";
}

}  // namespace raw::gpu_check
