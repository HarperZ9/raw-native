// raw_native_cli material-gallery: see raw/tools/gallery_cmd.hpp.
#include "raw/tools/gallery_cmd.hpp"
#include "raw/renderer/gallery.hpp"
#include "raw/renderer/colour.hpp"
#include "raw/renderer/lighting_parity.hpp"
#include "raw/tools/image_out.hpp"
#include "raw/tools/gallery_pt.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
namespace raw {
namespace {
using gallery::Rgb;
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

}  // namespace

int galleryCommand(int argc, char** argv) {
    std::string out = "gallery";
    int size = 512, spp = 2;
    bool gpu = true, pt = false, field = false, occl = false;
    int height = 0;
    double spacing = 0.0;
    gallery::PtErrorDesc ptd;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--spp") && i + 1 < argc) spp = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cpu-only")) gpu = false;
        else if (!std::strcmp(argv[i], "--pt")) pt = true;
        else if (!std::strcmp(argv[i], "--field")) field = true;
        else if (!std::strcmp(argv[i], "--occlusion")) occl = true;
        else if (!std::strcmp(argv[i], "--spacing") && i + 1 < argc) spacing = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--pt-spp") && i + 1 < argc) ptd.spp = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--pt-bounces") && i + 1 < argc) ptd.bounces = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--pt-tess") && i + 2 < argc) { ptd.nu = std::atoi(argv[++i]); ptd.nv = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--pt-env-pi")) ptd.envScale = 3.14159265358979323846;
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) ptd.threads = std::atoi(argv[++i]);
        else { std::printf("usage: raw_native_cli material-gallery [--out DIR] [--field | --spacing S] [--occlusion] [--size N] [--height H] [--spp K] [--cpu-only] [--pt [--pt-spp N] [--pt-bounces B] [--pt-env-pi] [--pt-tess NU NV]] [--threads T]\n"); return 2; }
    }
    if (height == 0) height = field ? size * 5 / 8 : size;
    if (size < 16 || size > 4096 || height < 16 || height > 4096 || spp < 1 || spp > 8 || ptd.spp < 1 || ptd.bounces < 1 || ptd.nu < 8 || ptd.nv < 4) { std::printf("material-gallery: --size 16..4096, --spp 1..8, --pt-spp and --pt-bounces at least 1\n"); return 2; }
    std::filesystem::create_directories(out);
    const int threads = ptd.threads > 0 ? ptd.threads : int(std::max(1u, std::thread::hardware_concurrency()));
    const auto t0 = Clock::now();
    gallery::Gallery g = field ? gallery::buildField(size, height, spp) : (spacing > 0.0 ? gallery::buildSpaced(size, spp, spacing) : gallery::build(size, size, spp));
    if (occl) gallery::occlusion(g, 256, 64, ptd.threads);   // evidence/gallery-occlusion-bounds.json
    if (std::getenv("RAW_NATIVE_GALLERY_NOLIGHTS")) g.lights.clear();   // diagnosis: image lighting alone
    const pbr::Tables tables;
    lighting::Cube pre = g.env;
    lighting::prefilter(pre, lighting::kPrefilterSamples);
    const std::vector<Rgb> cpu = gallery::shadeCpu(g, tables, pre, threads);
    const double cpuMs = msSince(t0);
    if (!writeImages(out + "/gallery-cpu", g.width, g.height, gallery::resolve(g, cpu))) { std::printf("material-gallery: cannot write %s\n", out.c_str()); return 2; }
    if (pt) {   // the path-traced error (evidence/rt-v3-bounds.json c8)
        std::vector<Rgb> img;
        const std::string pj = gallery::ptError(g, cpu, tables, pre, ptd, img);
        writeImages(out + "/gallery-pt", g.width, g.height, img);
        std::ofstream(out + "/gallery-pt.json", std::ios::binary) << pj;
        std::printf("%s", pj.c_str());
    }
    std::string gpuJson = "null", why;
    if (gpu) {
        rhi::Device* dev = rhi::device(why);
        std::vector<float> gs;
        const auto t1 = Clock::now();
        gpu_check::DispatchTiming timing;
        if (dev && gpu_check::shadeOnGpu(*dev, g.grid, g.samples, g.lights, g.env, g.sh, g.exposure, gs, why, &timing, 21)) {
            const double gpuMs = msSince(t1);
            std::vector<Rgb> per(g.samples.size());
            double se = 0.0, worst = 0.0;
            const bool dump = std::getenv("RAW_NATIVE_GALLERY_DUMP") != nullptr;   // diagnosis: samples far from the reference
            for (std::size_t k = 0; k < per.size(); ++k) {
                per[k] = {gs[k * 3], gs[k * 3 + 1], gs[k * 3 + 2]};
                if (dump && std::fabs(per[k].r - cpu[k].r) > 2e-3 * std::fabs(cpu[k].r) + 1e-5) {
                    const lighting::Sample& s = g.samples[k];
                    std::fprintf(stderr, "sample %zu pixel %d n.v %.5f gpu %.6g cpu %.6g rough %.3f irid %.2f\n", k, g.pixel[k],
                                 s.n.x * s.v.x + s.n.y * s.v.y + s.n.z * s.v.z, per[k].r, cpu[k].r, s.m.roughness, s.m.iridescence);
                }
                for (double d : {per[k].r - cpu[k].r, per[k].g - cpu[k].g, per[k].b - cpu[k].b}) { se += d * d; worst = std::max(worst, std::fabs(d)); }
            }
            writeImages(out + "/gallery-gpu", g.width, g.height, gallery::resolve(g, per));
            char b[512];
            std::snprintf(b, sizeof b, "{\"backend\": \"%s\", \"adapter\": \"%s\", \"wall_ms\": %.1f, \"dispatch_ms\": %.3f, \"dispatch_chunks\": %d, \"dispatch_repeats\": %d, \"rmse_exposed\": %.3e, \"max_abs_exposed\": %.3e}",
                          dev->backendName(), dev->adapter().description.c_str(), gpuMs, timing.ms, timing.chunks, timing.repeats,
                          std::sqrt(se / (3.0 * double(per.size()) + 1e-300)), worst);
            gpuJson = b;
        } else {
            gpuJson = "{\"error\": \"" + why + "\"}";
        }
    }
    std::string rows;
    for (std::size_t r = 0; r < g.groupLabels.size(); ++r) rows += std::string(r ? ", " : "") + "\"" + g.groupLabels[r] + "\"";
    // The CPU frame's hash (FNV-1a over the exposed float64 pixels), the regression record.
    std::uint64_t fnv = 1469598103934665603ull;
    for (const Rgb& c : gallery::resolve(g, cpu)) for (double v : {c.r, c.g, c.b}) {
        const unsigned char* q = reinterpret_cast<const unsigned char*>(&v);
        for (std::size_t k = 0; k < sizeof v; ++k) { fnv ^= q[k]; fnv *= 1099511628211ull; }
    }
    char fh[24]; std::snprintf(fh, sizeof fh, "%016llx", static_cast<unsigned long long>(fnv));
    std::ofstream j(out + "/gallery.json", std::ios::binary);
    j << "{\n \"what\": \"raw-native " << g.name << "\",\n \"size\": " << size << ",\n \"width\": " << g.width << ", \"height\": " << g.height << ",\n \"cpu_frame_fnv\": \"" << fh << "\"" << ",\n \"spp\": " << spp * spp << ",\n \"samples\": " << g.samples.size()
      << ",\n \"ev100\": " << gallery::kEv100 << ",\n \"display\": \"agx/srgb\",\n \"rows\": [" << rows << "],\n \"cpu_wall_ms\": " << cpuMs
      << ",\n \"gpu\": " << gpuJson << "\n}\n";
    std::printf("material-gallery: %s (%dx%d, %d samples a pixel)\n", out.c_str(), g.width, g.height, spp * spp);
    return 0;
}
}  // namespace raw
