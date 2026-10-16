// The palette dither's checks and its GPU runner: bounds and controls of
// evidence/cpp-port-style-bounds-4.json, written before the first run.
#include "raw/renderer/style_dither.hpp"
#include "raw/renderer/style_parity.hpp"
#include "../data/bluenoise64.hpp"
#include "style_fixture.hpp"
#include "style_gpu.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gpu_check {
namespace {
using assets::Json;
using rhi::Access;
using rhi::BufferUsage;
namespace fx = style_fx;
constexpr double kTie = 1e-9;
constexpr long kMaxExcused = 2;

struct DitherGpuParams { std::uint32_t width, height, n, mode; };
static_assert(sizeof(DitherGpuParams) == 16, "DitherGpuParams layout");
// web/shaders/dither/ds.wgsl.mjs dsSplit: an f64 as an (hi, lo) f32 pair.
void split(std::vector<float>& v, double x) { const float hi = float(x); v.push_back(hi); v.push_back(float(x - double(hi))); }

bool runDither(rhi::Device& dev, const style::DitherPalette& pal, const style::DitherOptions& o, const std::vector<std::uint8_t>& plate, int w, int h,
               std::vector<std::uint32_t>& idx, std::string& err) {
    util::Gpu gpu(dev);
    const std::uint32_t n = std::uint32_t(w * h);
    const DitherGpuParams P{std::uint32_t(w), std::uint32_t(h), std::uint32_t(pal.entries.size()), std::uint32_t(o.mode)};
    // dither.mjs packConstants: lin[256], M1, M2, epsilon, pair penalty, IGN, brightness, strength.
    std::vector<float> K;
    for (int k = 0; k < 256; ++k) split(K, style::srgb8ToLinear(k));
    const double m1[9] = {0.4122214708, 0.5363325363, 0.0514459929, 0.2119034982, 0.6806995451, 0.1073969566, 0.0883024619, 0.2817188376, 0.6299787005};
    const double m2[9] = {0.2104542553, 0.7936177850, -0.0040720468, 1.9779984951, -2.4285922050, 0.4505937099, 0.0259040371, 0.7827717662, -0.8086757660};
    for (double x : m1) split(K, x);
    for (double x : m2) split(K, x);
    for (double x : {1e-9, style::pairPenalty(), 0.06711056, 0.00583715, 52.9829189, o.brightness, std::min(1.0, std::max(0.0, o.strength))}) split(K, x);
    std::vector<float> lab;
    std::vector<std::uint32_t> rgb, bn(4096), src(n);
    for (const style::PaletteEntry& e : pal.entries) {
        for (double x : e.lab) split(lab, x);
        rgb.push_back(std::uint32_t(e.rgb[0]) | (std::uint32_t(e.rgb[1]) << 8) | (std::uint32_t(e.rgb[2]) << 16) | (255u << 24));
    }
    for (std::size_t i = 0; i < 4096; ++i) bn[i] = style::data::kBlueNoise64[i];
    for (std::size_t i = 0; i < n; ++i) src[i] = std::uint32_t(plate[i * 4]) | (std::uint32_t(plate[i * 4 + 1]) << 8) | (std::uint32_t(plate[i * 4 + 2]) << 16) | (255u << 24);
    const auto ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "dither params");
    const auto kb = gpu.upload(K.data(), K.size() * 4, BufferUsage::Storage, "dither constants");
    const auto lb = gpu.upload(lab.data(), lab.size() * 4, BufferUsage::Storage, "dither palette");
    const auto rb = gpu.upload(rgb.data(), rgb.size() * 4, BufferUsage::Storage, "dither palette rgb");
    const auto nb = gpu.upload(bn.data(), bn.size() * 4, BufferUsage::Storage, "blue noise");
    const auto sb = gpu.upload(src.data(), src.size() * 4, BufferUsage::Storage, "dither plate");
    const auto ib = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "dither indices");
    const auto o8 = gpu.make(std::uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "dither rgba");
    std::vector<std::uint32_t> packed;
    const Access R = Access::StorageRead, W = Access::StorageWrite;
    const bool ok = style_gpu::dispatchAndRead(gpu, style_gpu::passIndex("style_dither"), {ub, kb, lb, rb, nb, sb, ib, o8}, {Access::Uniform, R, R, R, R, R, W, W},
                                               {ib}, n, {&idx});
    if (!ok) err = gpu.err.empty() ? "dither dispatch failed" : gpu.err;
    return ok;
}
// The built-in control's palette (bounds-4): entry 0's OKLab L + 0.01 (pair modes) or its linear red + 0.01 (mix modes).
style::DitherPalette bent(style::DitherPalette p, bool mix) {
    if (mix) p.lin[0][0] += 0.01; else p.entries[0].lab[0] += 0.01;
    return p;
}
}  // namespace

// The dither cases; returns the number the bundle holds.
int ditherParity(rhi::Device* dev, const std::string& dir, StyleParity& r) {
    fx::Bundle b;
    if (!fx::loadBundle(dir + "/dither.rnfx", b, r.error)) return 0;
    const auto& cases = b.head.get("cases")->a;
    for (const Json& c : cases) {
        StyleCase s; s.pass = "dither"; s.id = c.get("id")->s;
        const std::string plateName = c.get("plate")->s;
        const Json& pd = *b.head.get("plates")->get(plateName);
        const int w = int(pd.get("width")->number("width")), h = int(pd.get("height")->number("height"));
        const auto plate = b.arr<std::uint8_t>("plate." + plateName, "u8");
        const style::DitherPalette pal = style::ditherPalette(c.get("palette")->s);
        const style::DitherOptions o{style::ditherMode(c.get("mode")->s), c.get("strength")->number("strength"), c.get("brightness")->number("brightness")};
        const bool mix = std::uint32_t(o.mode) >= 6;
        const Json& lab = *c.get("lab");
        s.presetEqual = lab.a.size() == pal.entries.size();
        for (std::size_t k = 0; k < pal.entries.size() && s.presetEqual; ++k)
            for (std::size_t e = 0; e < 3; ++e) s.presetEqual = s.presetEqual && std::abs(pal.entries[k].lab[e] - lab.at(k).at(e).number("lab")) <= 1e-12;
        std::vector<double> margin;
        const auto mine = style::ditherPlate(plate, w, h, pal, o, &margin), other = style::ditherPlate(plate, w, h, bent(pal, mix), o);
        const auto js = b.arr<std::uint8_t>(s.id + ".js_idx", "u8");
        long controlDiff = 0;
        for (std::size_t i = 0; i < js.size(); ++i) {
            s.decisions++;
            controlDiff += other[i] != js[i];
            if (mine[i] != js[i]) (margin[i] <= kTie ? s.cpuExcused : s.cpuMismatch)++;
        }
        s.cpuControlFails = controlDiff > 0;
        s.cpuPass = s.presetEqual && s.decisions == long(w) * h && s.cpuMismatch == 0 && s.cpuExcused <= kMaxExcused && s.cpuControlFails;
        if (dev) {
            std::vector<std::uint32_t> got;
            if (!runDither(*dev, pal, o, plate, w, h, got, r.error)) { r.cases.push_back(s); return int(cases.size()); }
            s.gpuRun = true;
            long bentDiff = 0;
            for (std::size_t i = 0; i < got.size(); ++i) { s.gpuMismatch += got[i] != mine[i]; bentDiff += got[i] != other[i]; }
            s.gpuMismatchFraction = double(s.gpuMismatch) / double(s.decisions);
            const double bound = mix ? 1e-5 : 0.0;
            s.gpuControlFails = double(bentDiff) / double(s.decisions) > bound;
            s.gpuPass = s.gpuMismatchFraction <= bound && s.gpuControlFails;
        }
        r.cases.push_back(s);
    }
    return int(cases.size());
}

}  // namespace raw::gpu_check
