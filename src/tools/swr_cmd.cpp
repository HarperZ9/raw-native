// raw_native_cli swr-render: see raw/tools/swr_cmd.hpp.
#include "raw/tools/swr_cmd.hpp"
#include "raw/renderer/swr.hpp"
#include "raw/renderer/swr_parity.hpp"
#include "raw/renderer/swr_scenes.hpp"
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
    std::ofstream(out + "/swr.json") << js << "\n ]\n}\n";
    return 0;
}

}  // namespace raw
