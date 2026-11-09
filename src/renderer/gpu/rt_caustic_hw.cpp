// Effect C1 on hardware ray queries from the host (raw/renderer/rt_caustic.hpp causticHw).
#include "../rt_caustic_math.hpp"
#include <algorithm>
#include <cstring>
namespace raw::rt {
namespace {
using namespace caustic;
std::uint32_t asWord(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
std::vector<std::uint32_t> words(const Packed& p, const Cam& c, const CausticDesc& d, std::uint32_t flags, std::uint32_t begin,
                                 std::uint32_t count, std::uint32_t round, double invTotal) {
    const std::uint64_t seedL = d.seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    std::vector<std::uint32_t> w(36, 0u);
    const std::uint32_t head[12] = {std::uint32_t(d.width), std::uint32_t(d.height), begin, count, d.maxBounces, flags,
                                    std::uint32_t(d.seed), std::uint32_t(d.seed >> 32), p.lights, p.matOff, p.glassOff, p.lightOff};
    std::memcpy(w.data(), head, sizeof head);
    const float f[16] = {c.eye.x, c.eye.y, c.eye.z, c.tanHalf, c.fwd.x, c.fwd.y, c.fwd.z, c.aspect,
                         c.side.x, c.side.y, c.side.z, 0.0f, c.up.x, c.up.y, c.up.z, 0.0f};
    for (int i = 0; i < 16; ++i) w[12 + std::size_t(i)] = asWord(f[i]);
    w[28] = asWord(float(invTotal)); w[29] = asWord(float(d.controls.indexOffset)); w[30] = round;
    w[31] = std::uint32_t(seedL); w[32] = std::uint32_t(seedL >> 32);
    return w;
}
}  // namespace

CausticImage causticHw(rhi::Device& dev, const CausticScene& s, const CausticDesc& d, const float offset[3]) {
    CausticImage im;
    im.width = d.width; im.height = d.height;
    const Packed p = pack(s);
    const Cam cam = camOf(s.camera, d.width, d.height);
    const std::uint32_t flags = (d.split ? F_SPLIT : 0u) | (d.controls.noDispersion ? F_NODISP : 0u) | (d.controls.dropSplatCosine ? F_DROPCOS : 0u);
    const std::uint64_t npx = std::uint64_t(d.width) * std::uint64_t(d.height);
    rhi::hw::SpectralPassInput in;
    in.width = std::uint32_t(d.width); in.height = std::uint32_t(d.height);
    in.tris = s.tris; in.geo = p.geo; in.tables = p.tables;
    if (offset) for (int k = 0; k < 3; ++k) in.offset[k] = offset[k];
    // Camera samples a submission: about 3.7 million pixel-samples, so no submission nears the watchdog.
    const std::uint32_t slice = std::uint32_t(std::clamp<std::uint64_t>(3686400ull / std::max<std::uint64_t>(1, npx), 1, 64));
    for (std::uint32_t b = 0; b < d.spp;) {
        const std::uint32_t n = std::min(slice, d.spp - b);
        in.cameraParams.push_back(words(p, cam, d, flags | (b == 0 ? F_FIRST : 0u), b, n, 0, 0.0));
        b += n;
    }
    const std::uint32_t rounds = d.split ? d.photonsPerPixel : 0u;
    const double invTotal = rounds ? 1.0 / (double(npx) * rounds) : 0.0;
    for (std::uint32_t k = 0; k < rounds; ++k) in.lightParams.push_back(words(p, cam, d, flags, 0, 0, k, invTotal));
    const rhi::hw::SpectralPassResult r = rhi::hw::spectralPasses(dev, in);
    im.timing = r.timing; im.lightMs = r.lightMs;
    if (!r.ran) { im.error = r.error.empty() ? "the hardware caustic tracer did not run" : r.error; return im; }
    im.cameraXyz.resize(npx * 3); im.lightXyz.resize(npx * 3); im.xyz.resize(npx * 3);
    for (std::uint64_t q = 0; q < npx; ++q)
        for (int ch = 0; ch < 3; ++ch) {
            const double c = d.spp ? double(r.camera[q * 4 + ch]) / double(d.spp) : 0.0;
            const double l = double(r.splat[q * 3 + ch]) * (1.0 / 4294967296.0);
            im.cameraXyz[q * 3 + ch] = c; im.lightXyz[q * 3 + ch] = l; im.xyz[q * 3 + ch] = c + l;
        }
    return im;
}

}  // namespace raw::rt
