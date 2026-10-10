// raw_native_cli swr-render: see raw/tools/swr_cmd.hpp.
#include "raw/tools/swr_cmd.hpp"
#include "raw/renderer/swr.hpp"
#include "raw/renderer/swr_parity.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/renderer/rt_hybrid.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include <algorithm>
#include <cmath>
#include "raw/tools/image_out.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
std::vector<std::uint8_t> rgbOf(const swr::Visibility& v, const swr::Resolved& r) {
    std::vector<std::uint8_t> px(v.slot.size() * 3);
    for (std::size_t p = 0; p < v.slot.size(); ++p) {
        const std::uint32_t c = v.slot[p] ? r.colour[p] : 0x00281e1au;   // an ink ground where nothing covers
        px[p * 3] = std::uint8_t(c & 255u); px[p * 3 + 1] = std::uint8_t((c >> 8) & 255u); px[p * 3 + 2] = std::uint8_t((c >> 16) & 255u);
    }
    return px;
}
std::vector<std::uint8_t> idsOf(const swr::Visibility& v) {
    std::vector<std::uint8_t> px(v.slot.size() * 3);
    for (std::size_t p = 0; p < v.slot.size(); ++p) {
        if (!v.slot[p]) continue;
        std::uint32_t h = (v.slot[p] - 1) / swr::kSlotsPerTri * 0x9e3779b1u;
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
        px[p * 3] = std::uint8_t(64 + (h & 127u)); px[p * 3 + 1] = std::uint8_t(64 + ((h >> 8) & 127u)); px[p * 3 + 2] = std::uint8_t(64 + ((h >> 16) & 127u));
    }
    return px;
}
long differing(const swr::Visibility& a, const swr::Resolved& ra, const swr::Visibility& b, const swr::Resolved& rb) {
    long d = 0;
    for (std::size_t p = 0; p < a.slot.size(); ++p) d += a.slot[p] != b.slot[p] || (a.slot[p] && ra.colour[p] != rb.colour[p]);
    return d;
}
// The hybrid showcase: the GPU raster colour, darkened where the ray-traced sun shadow falls,
// with a mirror term on the tiles (0.25) and brass (0.5) from the ray-traced reflection, the
// reflected texel fetched on the host from the hit point. Showcase only; H1 and H2 check the rays.
std::vector<std::uint8_t> hybridImage(rhi::Device& dev, const rt::PtScene& ps, const swr::Scene& base, int w, int h) {
    const gpu_check::HybridFrame hy = gpu_check::hybridGpu(dev, ps, base, w, h);
    const gpu_check::SwrFrame f = gpu_check::swrGpu(dev, ps.geo, ps.tex, base.light, base.viewProj(w, h), w, h, swr::Options{}, nullptr);
    std::vector<std::uint8_t> px(std::size_t(w) * std::size_t(h) * 3, 0);
    if (!hy.error.empty() || !f.error.empty()) return px;
    const std::vector<Tri> tris = rt::trianglesOf(ps.geo);
    const swr::Geometry& g = ps.geo;
    for (std::size_t p = 0; p < f.vis.slot.size(); ++p) {
        std::uint32_t c = f.vis.slot[p] ? f.res.colour[p] : 0x00281e1au;
        float rgb[3] = {float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u)};
        if (f.vis.slot[p]) {
            const float shade = hy.lit[p] > 0.5f ? 1.0f : 0.45f;
            for (float& v : rgb) v *= shade;
            const std::uint32_t tex = f.res.texel[p] >> 24;
            const float k = tex == swr::kTiles ? 0.25f : tex == swr::kBrass ? 0.5f : 0.0f;
            if (k > 0.0f && hy.reflTri[p] >= 0) {
                const Vec3 pos{hy.gbuffer[p * 12], hy.gbuffer[p * 12 + 1], hy.gbuffer[p * 12 + 2]};
                const Vec3 n{hy.gbuffer[p * 12 + 6], hy.gbuffer[p * 12 + 7], hy.gbuffer[p * 12 + 8]};
                const Vec3 d = normalize(pos - base.eye), r = d - n * (2.0f * dot(d, n));
                const std::size_t t = std::size_t(hy.reflTri[p]);
                float tt, u, v;
                if (intersectTri({pos + r * 1e-3f, r}, tris[t], tt, u, v)) {
                    const std::uint32_t i0 = g.idx[t * 3], i1 = g.idx[t * 3 + 1], i2 = g.idx[t * 3 + 2];
                    const float w0 = 1.0f - u - v, su = g.uv[i0].x * w0 + g.uv[i1].x * u + g.uv[i2].x * v, sv = g.uv[i0].y * w0 + g.uv[i1].y * u + g.uv[i2].y * v;
                    const swr::TextureSet::Entry& e = ps.tex.entries[g.triTexture[t]];
                    const std::uint32_t q = ps.tex.texels[e.offset + (std::uint32_t(std::int32_t(std::floor(sv * float(e.height)))) & (e.height - 1)) * e.width +
                                                          (std::uint32_t(std::int32_t(std::floor(su * float(e.width)))) & (e.width - 1))];
                    const float m[3] = {float(q & 255u), float((q >> 8) & 255u), float((q >> 16) & 255u)};
                    for (int j = 0; j < 3; ++j) rgb[j] = rgb[j] * (1.0f - k) + m[j] * k;
                }
            }
        }
        for (int j = 0; j < 3; ++j) px[p * 3 + std::size_t(j)] = std::uint8_t(std::min(255.0f, rgb[j] + 0.5f));
    }
    return px;
}
}  // namespace

int swrRenderCommand(int argc, char** argv) {
    std::string out = "swr";
    bool gpu = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--cpu-only")) gpu = false;
        else { std::printf("usage: raw_native_cli swr-render [--out DIR] [--cpu-only]\n"); return 2; }
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec || !std::filesystem::is_directory(out)) { std::printf("swr-render: cannot create %s\n", out.c_str()); return 2; }
    rhi::Device* dev = nullptr;
    std::string why;
    if (gpu) dev = rhi::device(why);
    swr::Options modern, retro;
    retro.snapShift = 8; retro.affine = true; retro.depthBits = 12;
    struct Frame { const char* tag; swr::Options o; int w, h; };
    const Frame frames[2] = {{"modern", modern, 640, 480}, {"retro", retro, 320, 240}};
    std::string js = "{\n \"backend\": \"" + std::string(dev ? dev->backendName() : "none") + "\",\n \"frames\": [\n";
    bool first = true;
    for (const swr::Scene& sc : swr::ownedScenes())
        for (const Frame& f : frames) {
            const Mat4 M = sc.viewProj(f.w, f.h);
            const auto t0 = Clock::now();
            const swr::Setup s = swr::setup(sc.geo, M, f.w, f.h, f.o);
            const swr::Visibility v = swr::rasterize(s, f.o);
            const swr::Resolved r = swr::resolve(s, v, sc.geo, sc.tex, sc.light, f.o);
            const double cpuMs = msSince(t0);
            const std::string stem = out + "/" + sc.name + "-" + f.tag;
            if (!writePng(stem + ".png", f.w, f.h, rgbOf(v, r))) { std::printf("swr-render: cannot write %s.png\n", stem.c_str()); return 2; }
            if (std::string(f.tag) == "modern") writePng(out + "/" + sc.name + "-ids.png", f.w, f.h, idsOf(v));
            char b[512];
            if (dev) {
                const auto t1 = Clock::now();
                const gpu_check::SwrFrame g = gpu_check::swrGpu(*dev, sc.geo, sc.tex, sc.light, M, f.w, f.h, f.o, nullptr);
                const double gpuMs = msSince(t1);
                if (!g.error.empty()) { std::printf("swr-render: GPU: %s\n", g.error.c_str()); return 2; }
                writePng(stem + "-gpu.png", f.w, f.h, rgbOf(g.vis, g.res));
                std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"frame\": \"%s\", \"size\": [%d, %d], \"triangles\": %zu, \"cpu_ms\": %.1f, \"gpu_ms_with_transfers\": %.1f, \"pixels_differing\": %ld}",
                              sc.name.c_str(), f.tag, f.w, f.h, sc.geo.triangles(), cpuMs, gpuMs, differing(v, r, g.vis, g.res));
            } else {
                std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"frame\": \"%s\", \"size\": [%d, %d], \"triangles\": %zu, \"cpu_ms\": %.1f}",
                              sc.name.c_str(), f.tag, f.w, f.h, sc.geo.triangles(), cpuMs);
            }
            js += (first ? "" : ",\n") + std::string(b);
            first = false;
            std::printf("%s\n", b);
        }
    if (dev) {   // hybrid frames: raster plus ray-traced shadows and reflections
        const std::pair<rt::PtScene, swr::Scene> hs[2] = {{rt::ptRetroRoom(), swr::retroRoom()}, {rt::ptIsoStreet(), swr::isoStreet()}};
        for (const auto& [ps, base] : hs) writePng(out + "/" + base.name + "-hybrid.png", 640, 480, hybridImage(*dev, ps, base, 640, 480));
    }
    std::ofstream(out + "/swr.json") << js << "\n ]\n}\n";
    return 0;
}

}  // namespace raw
