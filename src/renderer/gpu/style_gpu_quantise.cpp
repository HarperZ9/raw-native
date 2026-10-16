// Hysteresis quantisation and palette-locked lighting on an RHI device: see style_gpu_quantise.hpp.
#include "style_gpu_quantise.hpp"
#include "style_gpu.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check::style_gpu {
namespace {
using rhi::Access;
using rhi::BufferUsage;
void put4(float* o, double a, double b, double c, double d) { o[0] = float(a); o[1] = float(b); o[2] = float(c); o[3] = float(d); }
}  // namespace

struct GpuHysteresis::Impl {
    rhi::Device& dev;
    style::HystPlan plan;
    int w, h, frameNo{0};
    util::Gpu gpu;
    rhi::BufferHandle state[2];
    std::vector<float> pal;
    Impl(rhi::Device& d, const style::HystPlan& p, int ww, int hh) : dev(d), plan(p), w(ww), h(hh), gpu(d) {
        const std::uint64_t bytes = std::uint64_t(w) * h * 16;
        for (auto& s : state) s = gpu.make(bytes, BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, "hyst state");
        pal.assign(std::max<std::size_t>(1, p.pal.size()) * 4, 0.0f);
        for (std::size_t k = 0; k < p.pal.size(); ++k) for (std::size_t c = 0; c < 3; ++c) pal[k * 4 + c] = float(p.pal[k].lab[c]);
    }
};
GpuHysteresis::GpuHysteresis(rhi::Device& dev, const style::HystPlan& p, int w, int h) : impl_(std::make_unique<Impl>(dev, p, w, h)) {}
GpuHysteresis::~GpuHysteresis() = default;

bool GpuHysteresis::frame(const std::vector<float>& rgba, const std::vector<float>& mv, const std::vector<float>& dA, const std::vector<float>& dist,
                          std::vector<std::int32_t>& idx, std::vector<std::uint32_t>& packed, std::string& err) {
    Impl& m = *impl_;
    const std::uint32_t n = std::uint32_t(m.w * m.h);
    const bool motion = !mv.empty();
    HystParamsGpu P{std::uint32_t(m.w), std::uint32_t(m.h), m.plan.bands ? 0u : 1u, std::uint32_t(m.plan.pal.size()), m.frameNo > 0 ? 1u : 0u,
                    motion ? 1u : 0u, 1u, 0u, {}, {}};
    put4(P.a, m.plan.N, m.plan.margin, m.plan.gain, m.plan.depthTol); put4(P.b, m.plan.K, 0, 0, 0);
    // web/shaders/hysteresis/gpu.mjs packMotion: (mx, my, expected previous distance, this frame's distance).
    std::vector<float> mo(std::size_t(n) * 4, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        mo[i * 4] = motion ? mv[i * 2] : 0.0f; mo[i * 4 + 1] = motion ? mv[i * 2 + 1] : 0.0f;
        mo[i * 4 + 2] = motion ? dA[i] : dist[i]; mo[i * 4 + 3] = dist[i];
    }
    const rhi::BufferHandle prev = m.state[(m.frameNo + 1) % 2], next = m.state[m.frameNo % 2];
    const auto ub = m.gpu.upload(&P, sizeof P, BufferUsage::Uniform, "hyst params");
    const auto pb = m.gpu.upload(m.pal.data(), m.pal.size() * 4, BufferUsage::Storage, "hyst palette");
    const auto ib = m.gpu.upload(rgba.data(), rgba.size() * 4, BufferUsage::Storage, "hyst frame");
    const auto mb = m.gpu.upload(mo.data(), mo.size() * 4, BufferUsage::Storage, "hyst motion");
    const auto o8 = m.gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "hyst packed");
    std::vector<std::uint32_t> st;
    const Access R = Access::StorageRead, W = Access::StorageWrite, C = Access::CopyDst, U = Access::Undefined;
    const bool ok = dispatchAndReadEx(m.gpu, passIndex("style_hysteresis"), {ub, pb, ib, mb, prev, next, o8}, {Access::Uniform, R, R, R, R, W, W},
                                      {C, C, C, C, U, U, U}, {next, o8}, {4u, 1u}, n, {&st, &packed});
    if (!ok) { err = m.gpu.err.empty() ? "hysteresis dispatch failed" : m.gpu.err; return false; }
    idx.resize(n);
    for (std::size_t i = 0; i < n; ++i) { float x; std::memcpy(&x, &st[i * 4], 4); idx[i] = std::int32_t(std::floor(x + 0.5f)); }
    m.frameNo++;
    return true;
}

bool runPalettelight(rhi::Device& dev, const style::PalettelightPlan& p, const style::ChapelGBuffer& g, double time,
                     std::vector<std::uint32_t>& idx, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(g.w * g.h);
    PalettelightParamsGpu P{std::uint32_t(g.w), std::uint32_t(g.h), std::uint32_t(p.steps), std::uint32_t(p.voidSteps), {}, {}, {}, {}, {}, {}, {}, {}};
    put4(P.a, p.gain, p.ambient, p.moon, p.cycleRate); put4(P.b, time, p.moonDir[0], p.moonDir[1], p.moonDir[2]);
    auto light = [](float* a, float* b, const style::DitherLight& L) { put4(a, L.pos[0], L.pos[1], L.pos[2], L.radius); put4(b, L.core, L.intensity, L.wrap, 0); };
    light(P.lampA, P.lampB, p.lamp); light(P.g0a, P.g0b, p.glows[0]); light(P.g1a, P.g1b, p.glows[1]);
    // web/shaders/palettelight/gpu.mjs packGbuffer, packTable and packEntries.
    std::vector<float> gb(std::size_t(n) * 16, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        float* o = gb.data() + i * 16;
        o[0] = g.hit[i]; o[1] = g.cls[i]; o[2] = g.a[i]; o[3] = g.phase[i];
        for (std::size_t c = 0; c < 3; ++c) { o[4 + c] = g.P[i * 3 + c]; o[8 + c] = g.N[i * 3 + c]; }
        o[7] = g.vis[i * 3]; o[11] = g.vis[i * 3 + 1]; o[12] = g.vis[i * 3 + 2];
    }
    const style::RampPalette& pal = p.palette;
    std::vector<std::uint32_t> tbl, ent;
    for (int c = 0; c < 4; ++c) for (int v = 0; v < 2; ++v) for (int s : pal.ramp[c][v]) tbl.push_back(std::uint32_t(s));
    for (int s : pal.voidRamp) tbl.push_back(std::uint32_t(s));
    for (int s : pal.window) tbl.push_back(std::uint32_t(s));
    for (int s : pal.lamp) tbl.push_back(std::uint32_t(s));
    tbl.push_back(std::uint32_t(pal.night));
    for (const style::PaletteEntry& e : pal.entries) ent.push_back(std::uint32_t(e.rgb[0]) | (std::uint32_t(e.rgb[1]) << 8) | (std::uint32_t(e.rgb[2]) << 16) | (255u << 24));
    const auto ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "pl params");
    const auto gbb = gpu.upload(gb.data(), gb.size() * 4, BufferUsage::Storage, "pl gbuffer");
    const auto tb = gpu.upload(tbl.data(), tbl.size() * 4, BufferUsage::Storage, "pl table");
    const auto eb = gpu.upload(ent.data(), ent.size() * 4, BufferUsage::Storage, "pl entries");
    const auto ib = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "pl index");
    const auto o8 = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "pl packed");
    const Access R = Access::StorageRead, W = Access::StorageWrite;
    const bool ok = dispatchAndRead(gpu, passIndex("style_palettelight"), {ub, gbb, tb, eb, ib, o8}, {Access::Uniform, R, R, R, W, W}, {ib, o8}, n, {&idx, &packed});
    if (!ok) err = gpu.err.empty() ? "palettelight dispatch failed" : gpu.err;
    return ok;
}

}  // namespace raw::gpu_check::style_gpu
