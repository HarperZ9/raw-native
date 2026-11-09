// raw_native_cli colonnade: the sunlit colonnade path-traced (raw/renderer/rt_colonnade.hpp;
// evidence/colonnade-bounds.json). The flags are the settings a live panel will expose.
//   raw_native_cli colonnade [--out DIR] [--width W] [--height H] [--spp N] [--bounces B]
//       [--sun-elev DEG] [--sun-az DEG] [--turbidity T] [--ev EV100] [--no-emission] [--threads T]
#include "raw/renderer/rt_colonnade.hpp"
#include "raw/tools/image_out.hpp"
#include "raw/tools/relight_cmd.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace raw {

int colonnadeCommand(int argc, char** argv) {
    std::string out = "colonnade";
    rt::ColonnadeParams p;
    int w = 480, h = 270, spp = 64, bounces = 64, threads = 0;
    double ev = 13.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--out" && more) out = argv[++i];
        else if (a == "--width" && more) w = std::atoi(argv[++i]);
        else if (a == "--height" && more) h = std::atoi(argv[++i]);
        else if (a == "--spp" && more) spp = std::atoi(argv[++i]);
        else if (a == "--bounces" && more) bounces = std::atoi(argv[++i]);
        else if (a == "--sun-elev" && more) p.sunElevation = std::atof(argv[++i]);
        else if (a == "--sun-az" && more) p.sunAzimuth = std::atof(argv[++i]);
        else if (a == "--turbidity" && more) p.turbidity = std::atof(argv[++i]);
        else if (a == "--ev" && more) ev = std::atof(argv[++i]);
        else if (a == "--no-emission") p.emission = false;
        else if (a == "--threads" && more) threads = std::atoi(argv[++i]);
        else {
            std::printf("usage: raw_native_cli colonnade [--out DIR] [--width W] [--height H] [--spp N] [--bounces B] [--sun-elev DEG] [--sun-az DEG] [--turbidity T] [--ev EV100] [--no-emission] [--threads T]\n");
            return 2;
        }
    }
    if (w < 8 || h < 8 || spp < 1 || bounces < 1 || p.turbidity < 1.7 || p.turbidity > 10.0 || p.sunElevation < 0.5 || p.sunElevation > 90.0) {
        std::printf("colonnade: sizes at least 8, spp and bounces at least 1, turbidity 1.7..10, sun elevation 0.5..90 degrees\n");
        return 2;
    }
    std::filesystem::create_directories(out);
    const auto t0 = std::chrono::steady_clock::now();
    const rt::PtScene s = rt::ptColonnade(p);
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    rt::PathTraceDesc d; d.width = w; d.height = h; d.spp = std::uint32_t(spp); d.maxBounces = std::uint32_t(bounces); d.seed = 1;
    d.camera = d.previous = rt::colonnadeCamera();
    const auto t1 = std::chrono::steady_clock::now();
    const rt::PathTraceOutput o = rt::pathTraceCpu(s, d, threads);
    const double traceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    if (!o.error.empty()) { std::printf("colonnade: %s\n", o.error.c_str()); return 2; }
    const double ex = 1.0 / (1.2 * std::pow(2.0, ev));
    std::vector<lighting::Rgb> img(std::size_t(w) * h);
    for (std::size_t i = 0; i < img.size(); ++i) img[i] = {o.radiance[i * 3] * ex, o.radiance[i * 3 + 1] * ex, o.radiance[i * 3 + 2] * ex};
    if (!writeImages(out + "/colonnade", w, h, img)) { std::printf("colonnade: cannot write %s\n", out.c_str()); return 2; }
    char b[768];
    std::snprintf(b, sizeof b,
        "{\n \"scene\": \"colonnade\",\n \"width\": %d, \"height\": %d, \"spp\": %d, \"bounces\": %d,\n \"sun_elevation\": %.3f, \"sun_azimuth\": %.3f, \"turbidity\": %.3f, \"emission\": %s, \"ev100\": %.2f,\n"
        " \"path_tracer_version\": %d, \"threads\": %d,\n \"build_ms\": %.1f, \"trace_ms\": %.1f, \"us_per_sample_pixel\": %.4f\n}\n",
        w, h, spp, bounces, p.sunElevation, p.sunAzimuth, p.turbidity, p.emission ? "true" : "false", ev, rt::kPathTraceVersion, threads, buildMs, traceMs,
        traceMs * 1000.0 / (double(w) * h * spp));
    std::ofstream(out + "/colonnade.json", std::ios::binary) << b;
    std::printf("%s", b);
    return 0;
}
}  // namespace raw
