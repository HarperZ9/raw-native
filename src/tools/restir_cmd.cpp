// raw_native_cli restir-di: ReSTIR DI stage 1 on an owned scene (evidence/restir-di-bounds.json,
// r2 to r4 on the evidence scene).
//
//   raw_native_cli restir-di [scene flags as for a render] [--lights N] [--frames F] [--json FILE]
//
// The scene's triangles as the path tracer's (rt::ptFromScene) with its sun and sky off and N
// point lights scattered inside its bounding box; the exact mode's frame is the reference. For
// each sampled mode: the image mean over F seeds (with its standard error) and the single-frame
// relative RMSE against exact (mean of the first 16 seeds). Exit 0 when measured; it judges nothing.
#include "raw/renderer/rt_pt_convert.hpp"
#include "raw/renderer/rt_restir.hpp"
#include "raw/tools/cli_params.hpp"
#include "raw/tools/model_scene.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
double lum(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
double meanOf(const rt::DiOutput& o) {
    double s = 0; long n = 0;
    for (std::size_t i = 0; i < o.covered.size(); ++i) if (o.covered[i]) { s += lum(o.radiance.data() + 3 * i); ++n; }
    return n ? s / double(n) : 0.0;
}
double relRmse(const rt::DiOutput& a, const rt::DiOutput& r) {
    double d2 = 0, s = 0; long n = 0;
    for (std::size_t i = 0; i < r.covered.size(); ++i) if (r.covered[i]) {
        const double x = lum(a.radiance.data() + 3 * i), y = lum(r.radiance.data() + 3 * i);
        d2 += (x - y) * (x - y); s += y; ++n;
    }
    return n && s > 0 ? std::sqrt(d2 / double(n)) / (s / double(n)) : 0.0;
}
}  // namespace

int restirCommand(int argc, char** argv) {
    std::string json;
    int lights = 128, frames = 64;
    std::vector<const char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--json" && more) json = argv[++i];
        else if (a == "--lights" && more) lights = std::atoi(argv[++i]);
        else if (a == "--frames" && more) frames = std::atoi(argv[++i]);
        else rest.push_back(argv[i]);
    }
    std::string err;
    const auto p = parseArgs(int(rest.size()), rest.data(), err);
    if (!p || lights < 1 || frames < 16) {
        std::printf("usage: raw_native_cli restir-di [scene flags] [--lights N] [--frames F >= 16] [--json FILE]%s%s\n", err.empty() ? "" : "\n  ", err.c_str());
        return 2;
    }
    Scene sc = sceneFromParams(*p, nullptr);
    sc.camera = cameraFromParams(*p);
    rt::PtScene s = rt::ptFromScene(sc, 0.0f, true);
    s.sunIrradiance = {0, 0, 0}; s.sky = {0, 0, 0};
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (Vec3 q : s.geo.pos) { lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)}; hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)}; }
    const float ext = std::max(hi.x - lo.x, std::max(hi.y - lo.y, hi.z - lo.z));
    s.lights = rt::scatterLights(lights, lo, hi, 20261021, 1.0, 0.25 * ext);
    rt::finishPtScene(s);
    rt::DiDesc d;
    d.width = p->width; d.height = p->height; d.camera = rt::ptCameraOf(sc.camera); d.threads = p->threads;
    const auto t0 = std::chrono::steady_clock::now();
    d.mode = rt::DiMode::Exact;
    const rt::DiOutput ex = rt::directLightCpu(s, d);
    if (!ex.error.empty()) { std::printf("restir-di: %s\n", ex.error.c_str()); return 2; }
    const double exMean = meanOf(ex);
    char buf[512];
    std::string out = "{\n \"schema\": \"raw-native.restir-di/1\",\n \"scene\": \"" + (p->ownedScene.empty() ? std::string("test-scene") : p->ownedScene) +
                      "\",\n \"width\": " + std::to_string(d.width) + ", \"height\": " + std::to_string(d.height) + ", \"lights\": " + std::to_string(lights) +
                      ", \"frames\": " + std::to_string(frames) + ", \"candidates\": " + std::to_string(d.candidates) + ", \"neighbours\": " + std::to_string(d.neighbours) +
                      ", \"radius\": " + std::to_string(d.radius) + ",\n \"exact_mean\": " + std::to_string(exMean) + ", \"exact_shadow_rays\": " + std::to_string(ex.shadowRays);
    double uniformRmse = 0;
    for (rt::DiMode m : {rt::DiMode::Uniform, rt::DiMode::Ris, rt::DiMode::Restir, rt::DiMode::RestirBiased}) {
        d.mode = m;
        double s1 = 0, s2 = 0, rm = 0; std::uint64_t rays = 0;
        for (int f = 0; f < frames; ++f) {
            d.seed = std::uint64_t(f + 7);
            const rt::DiOutput o = rt::directLightCpu(s, d);
            const double v = meanOf(o);
            s1 += v; s2 += v * v; rays += o.shadowRays;
            if (f < 16) rm += relRmse(o, ex) / 16.0;
        }
        const double mean = s1 / frames, se = std::sqrt(std::max(0.0, s2 / frames - mean * mean) / (frames - 1));
        if (m == rt::DiMode::Uniform) uniformRmse = rm;
        const char* name = m == rt::DiMode::Uniform ? "uniform" : m == rt::DiMode::Ris ? "ris" : m == rt::DiMode::Restir ? "restir" : "restir_biased";
        std::snprintf(buf, sizeof buf, ",\n \"%s\": {\"mean\": %.6g, \"se\": %.3g, \"z\": %.3f, \"relative_rmse_single_frame\": %.5g, \"ratio_to_uniform\": %.4f, \"shadow_rays_per_frame\": %.1f}",
                      name, mean, se, se > 0 ? (mean - exMean) / se : 0.0, rm, uniformRmse > 0 ? rm / uniformRmse : 0.0, double(rays) / frames);
        out += buf;
    }
    std::snprintf(buf, sizeof buf, ",\n \"wall_ms\": %.1f\n}\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    out += buf;
    if (!json.empty()) std::ofstream(json, std::ios::binary) << out;
    std::printf("%s", out.c_str());
    return 0;
}
}  // namespace raw
