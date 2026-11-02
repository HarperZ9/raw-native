// raw_native_cli caustic (raw/tools/caustic_cmd.hpp; bounds in evidence/hw-c1-caustic-bounds.json).
#include "raw/tools/caustic_cmd.hpp"
#include "raw/renderer/rt_caustic.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
namespace raw {
namespace {
struct Args {
    std::string mode, out;
    rt::CausticDesc d;
    int view{0}, frames{30}, warmup{3};
    float exposure{1.0f}, lampScale{1.0f}, coneDeg{6.0f};
    bool cpu{false}, quick{false}, noGlass{false};
};
bool parse(int argc, char** argv, Args& a) {
    if (argc < 2) return false;
    a.mode = argv[1];
    a.d.width = 1280; a.d.height = 720;
    for (int i = 2; i < argc; ++i) {
        const bool more = i + 1 < argc;
        auto num = [&] { return std::atoi(argv[++i]); };
        if (!std::strcmp(argv[i], "--cpu")) a.cpu = true;
        else if (!std::strcmp(argv[i], "--quick")) a.quick = true;
        else if (!std::strcmp(argv[i], "--out") && more) a.out = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && more) a.d.width = num();
        else if (!std::strcmp(argv[i], "--height") && more) a.d.height = num();
        else if (!std::strcmp(argv[i], "--spp") && more) a.d.spp = std::uint32_t(num());
        else if (!std::strcmp(argv[i], "--photons") && more) a.d.photonsPerPixel = std::uint32_t(num());
        else if (!std::strcmp(argv[i], "--bounces") && more) a.d.maxBounces = std::uint32_t(num());
        else if (!std::strcmp(argv[i], "--seed") && more) a.d.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--view") && more) a.view = num();
        else if (!std::strcmp(argv[i], "--lamp-scale") && more) a.lampScale = std::strtof(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--cone") && more) a.coneDeg = std::strtof(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--bsdf-only")) a.d.split = false;
        else if (!std::strcmp(argv[i], "--no-glass")) a.noGlass = true;
        else if (!std::strcmp(argv[i], "--no-dispersion")) a.d.controls.noDispersion = true;
        else if (!std::strcmp(argv[i], "--frames") && more) a.frames = num();
        else if (!std::strcmp(argv[i], "--exposure") && more) a.exposure = std::strtof(argv[++i], nullptr);
        else return false;
    }
    return (a.mode == "render" || a.mode == "contract" || a.mode == "time") && a.d.width >= 8 && a.d.width <= 8192 &&
           a.d.height >= 8 && a.d.height <= 8192 && a.d.maxBounces <= 19 && a.frames >= 1 && (a.view == 0 || a.view == 1);
}
// A diagnostic: the scene without its glass, where the two estimators must agree with no light paths at all.
void dropGlass(rt::CausticScene& s) {
    rt::CausticScene o = s;
    o.tris.clear(); o.normals.clear(); o.material.clear();
    for (std::size_t t = 0; t < s.material.size(); ++t) {
        if (s.materials[s.material[t]].kind == rt::CausticKind::Glass) continue;
        o.tris.insert(o.tris.end(), s.tris.begin() + std::ptrdiff_t(t * 9), s.tris.begin() + std::ptrdiff_t(t * 9 + 9));
        o.normals.insert(o.normals.end(), s.normals.begin() + std::ptrdiff_t(t * 9), s.normals.begin() + std::ptrdiff_t(t * 9 + 9));
        o.material.push_back(s.material[t]);
    }
    s = std::move(o);
}
std::vector<double> sorted(std::vector<double> v) { std::sort(v.begin(), v.end()); return v; }
double pct(const std::vector<double>& s, double p) { return s.empty() ? 0.0 : s[std::min(s.size() - 1, std::size_t(p * double(s.size())))]; }
int render(rhi::Device* dev, const Args& a) {
    rt::CausticScene s = rt::causticPrismRoom(a.view, a.lampScale, a.coneDeg);
    if (a.noGlass) dropGlass(s);
    const auto t0 = std::chrono::steady_clock::now();
    const rt::CausticImage im = a.cpu ? rt::causticCpu(s, a.d) : rt::causticHw(*dev, s, a.d);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!im.error.empty()) { std::printf("caustic: %s\n", im.error.c_str()); return 1; }
    const std::string dir = a.out.empty() ? "caustic" : a.out;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::vector<float> rgb = rt::causticToRgb(im, a.exposure);
    std::vector<lighting::Rgb> img(rgb.size() / 3);
    for (std::size_t p = 0; p < img.size(); ++p) img[p] = {rgb[p * 3], rgb[p * 3 + 1], rgb[p * 3 + 2]};
    if (!writeImages(dir + "/caustic", im.width, im.height, img)) { std::printf("caustic: cannot write %s\n", dir.c_str()); return 1; }
    std::ofstream xyz(dir + "/caustic_xyz.f64", std::ios::binary);
    xyz.write(reinterpret_cast<const char*>(im.xyz.data()), std::streamsize(im.xyz.size() * 8));
    double cam = 0, lig = 0;
    for (double v : im.timing.sliceMs) cam += v;
    for (double v : im.lightMs) lig += v;
    char b[900];
    std::snprintf(b, sizeof b, "{\n \"scene\": \"%s\", \"width\": %d, \"height\": %d, \"spp\": %u, \"photons_per_pixel\": %u, \"bounces\": %u, \"seed\": %llu,\n"
                  " \"backend\": \"%s\", \"adapter\": \"%s\", \"exposure\": %.4f,\n \"build_ms\": %.3f, \"camera_gpu_ms\": %.2f, \"light_gpu_ms\": %.2f, \"wall_ms\": %.1f,\n"
                  " \"glasses\": [\"%s\", \"%s\"]\n}\n",
                  s.name.c_str(), im.width, im.height, a.d.spp, a.d.photonsPerPixel, a.d.maxBounces, (unsigned long long)a.d.seed,
                  a.cpu ? "cpu" : dev->backendName(), a.cpu ? "" : rhi::hw::jsonEscape(dev->adapter().description).c_str(), double(a.exposure),
                  im.timing.buildMs, cam, lig, ms, s.glasses[0].id.c_str(), s.glasses[1].id.c_str());
    std::ofstream(dir + "/caustic.json", std::ios::binary) << b;
    std::fputs(b, stdout);
    return 0;
}
// Frames of 1 camera sample and 1 photon a pixel, timed by GPU timestamps around each pass.
int timeFrames(rhi::Device& dev, const Args& a) {
    const rt::CausticScene s = rt::causticPrismRoom(a.view, a.lampScale, a.coneDeg);
    rt::CausticDesc d = a.d;
    d.spp = std::uint32_t(a.frames + a.warmup); d.photonsPerPixel = d.spp;
    const auto t0 = std::chrono::steady_clock::now();
    const rt::CausticImage im = rt::causticHw(dev, s, d);
    const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!im.error.empty()) { std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(im.error).c_str()); return 1; }
    if (im.timing.sliceMs.size() != d.spp || im.lightMs.size() != d.spp) { std::printf("{\"error\": \"a camera dispatch held more than one sample at this size\"}\n"); return 1; }
    std::vector<double> frame, cam, lig;
    for (std::size_t i = std::size_t(a.warmup); i < d.spp; ++i) {
        cam.push_back(im.timing.sliceMs[i]); lig.push_back(im.lightMs[i]); frame.push_back(im.timing.sliceMs[i] + im.lightMs[i]);
    }
    const auto f = sorted(frame), c = sorted(cam), l = sorted(lig);
    char b[900];
    std::snprintf(b, sizeof b, "{\n \"scene\": \"%s\", \"width\": %d, \"height\": %d, \"frames\": %d, \"warmup\": %d, \"adapter\": \"%s\",\n"
                  " \"frame_gpu_ms\": {\"median\": %.3f, \"p5\": %.3f, \"p95\": %.3f},\n \"camera_gpu_ms_median\": %.3f, \"light_gpu_ms_median\": %.3f,\n"
                  " \"build_ms\": %.3f, \"wall_ms\": %.1f\n}\n",
                  s.name.c_str(), d.width, d.height, a.frames, a.warmup, rhi::hw::jsonEscape(dev.adapter().description).c_str(),
                  pct(f, 0.5), pct(f, 0.05), pct(f, 0.95), pct(c, 0.5), pct(l, 0.5), im.timing.buildMs, wall);
    std::fputs(b, stdout);
    if (!a.out.empty()) std::ofstream(a.out, std::ios::binary) << b;
    return 0;
}
}  // namespace

int causticCommand(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        std::printf("usage: raw_native_cli caustic render [--width W] [--height H] [--spp N] [--photons N] [--bounces B] [--seed S] [--view 0|1]\n"
                    "                                   [--lamp-scale K] [--cone DEG] [--bsdf-only] [--no-dispersion] [--no-glass]\n"
                    "                                   [--exposure E] [--cpu] [--out DIR]\n"
                    "       raw_native_cli caustic contract [--quick] [--out FILE]\n"
                    "       raw_native_cli caustic time [--width W] [--height H] [--frames N] [--out FILE]\n");
        return 2;
    }
    std::string why;
    rhi::Device* dev = a.cpu && a.mode == "render" ? nullptr : rhi::device(why);
    if (!dev && !(a.cpu && a.mode == "render")) { std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    if (a.mode == "render") return render(dev, a);
    if (a.mode == "time") return timeFrames(*dev, a);
    const CausticReport r = causticContract(*dev, a.quick);
    std::fputs(r.json.c_str(), stdout);
    if (!a.out.empty()) std::ofstream(a.out, std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}

}  // namespace raw
