// raw_native_cli hw-pt (raw/tools/hw_pt_cmd.hpp; bounds in evidence/hw-h2-pt-bounds.json).
#include "raw/tools/hw_pt_cmd.hpp"
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_pt_hw.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/tools/image_out.hpp"
#include "rr_scenes.hpp"
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
    std::string mode, scene{"retro_room"}, out;
    rt::PathTraceDesc d;
    bool quick{false}, compute{false};
};
bool parse(int argc, char** argv, Args& a) {
    if (argc < 2) return false;
    a.mode = argv[1];
    a.d.width = 1920; a.d.height = 1080; a.d.spp = 64;
    for (int i = 2; i < argc; ++i) {
        const bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--quick")) a.quick = true;
        else if (!std::strcmp(argv[i], "--compute")) a.compute = true;
        else if (!std::strcmp(argv[i], "--out") && more) a.out = argv[++i];
        else if (!std::strcmp(argv[i], "--scene") && more) a.scene = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && more) a.d.width = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && more) a.d.height = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--spp") && more) a.d.spp = std::uint32_t(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--seed") && more) a.d.seed = std::strtoull(argv[++i], nullptr, 10);
        else return false;
    }
    return (a.mode == "contract" || a.mode == "render" || a.mode == "time") && a.d.width >= 8 && a.d.width <= 8192 &&
           a.d.height >= 8 && a.d.height <= 8192 && a.d.spp >= 1;
}
bool load(const std::string& name, rt::PtScene& s, rt::PtCamera& cam) {
    if (name == "retro_room") { s = rt::ptRetroRoom(); cam = rt::cameraOf(swr::retroRoom()); return true; }
    if (name == "iso_street") { s = rt::ptIsoStreet(); cam = rt::cameraOf(swr::isoStreet()); return true; }
    if (name == "dense_block") { s = rt::ptDenseBlock(); cam = rt::cameraOf(rt::denseBlock()); return true; }
    std::string lighting;
    return rr::scene(name, s, cam, lighting);
}
double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double sum(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s; }
// The compute tracer's GPU time is not exposed slice by slice, so it is timed by the wall clock of
// the whole render (submissions are waited one by one, so the wall clock covers the GPU work).
int timeBoth(rhi::Device& dev, const rt::PtScene& s, const Args& a) {
    const gpu_check::HwPtResult h = gpu_check::pathTraceHw(dev, s, a.d);
    if (!h.out.error.empty()) { std::printf("{\"error\": \"%s\", \"fallback\": true}\n", rhi::hw::jsonEscape(h.out.error).c_str()); return 1; }
    const auto t0 = std::chrono::steady_clock::now();
    const rt::PathTraceOutput g = gpu_check::pathTraceGpu(dev, s, a.d);
    const double gms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!g.error.empty()) { std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(g.error).c_str()); return 1; }
    char b[900];
    std::snprintf(b, sizeof b, "{\n \"scene\": \"%s\", \"width\": %d, \"height\": %d, \"spp\": %u,\n \"adapter\": \"%s\",\n"
                  " \"hardware\": {\"build_ms\": %.3f, \"slices\": %zu, \"median_slice_ms\": %.3f, \"trace_ms\": %.2f, \"finish_ms\": %.3f, \"wall_ms\": %.1f},\n"
                  " \"compute\": {\"wall_ms\": %.1f},\n \"wall_ratio_compute_over_hardware\": %.3f\n}\n",
                  a.scene.c_str(), a.d.width, a.d.height, a.d.spp, rhi::hw::jsonEscape(dev.adapter().description).c_str(),
                  h.timing.buildMs, h.timing.sliceMs.size(), median(h.timing.sliceMs), sum(h.timing.sliceMs), h.timing.finishMs, h.timing.wallMs,
                  gms, h.timing.wallMs > 0 ? gms / h.timing.wallMs : 0.0);
    std::fputs(b, stdout);
    if (!a.out.empty()) std::ofstream(a.out, std::ios::binary) << b;
    return 0;
}
int render(rhi::Device& dev, const rt::PtScene& s, const Args& a) {
    std::string note;
    rt::PathTraceOutput o;
    gpu_check::HwPtResult h;
    if (!a.compute) { h = gpu_check::pathTraceHw(dev, s, a.d); o = h.out; }
    if (a.compute || !o.error.empty()) { note = o.error; o = gpu_check::pathTraceGpu(dev, s, a.d); }
    if (!o.error.empty()) { std::printf("hw-pt: %s\n", o.error.c_str()); return 1; }
    const std::string dir = a.out.empty() ? "hw-pt" : a.out;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::vector<lighting::Rgb> img(std::size_t(o.width) * std::size_t(o.height));
    for (std::size_t p = 0; p < img.size(); ++p) img[p] = {o.radiance[p * 3], o.radiance[p * 3 + 1], o.radiance[p * 3 + 2]};
    if (!writeImages(dir + "/radiance", o.width, o.height, img)) { std::printf("hw-pt: cannot write %s\n", dir.c_str()); return 1; }
    char b[700];
    std::snprintf(b, sizeof b, "{\n \"scene\": \"%s\", \"width\": %d, \"height\": %d, \"spp\": %u, \"seed\": %llu,\n \"traversal\": \"%s\",\n"
                  " \"fallback_reason\": \"%s\",\n \"build_ms\": %.3f, \"trace_ms\": %.2f, \"wall_ms\": %.1f\n}\n",
                  a.scene.c_str(), o.width, o.height, o.spp, (unsigned long long)a.d.seed, note.empty() && !a.compute ? "hardware ray query" : "compute bvh",
                  rhi::hw::jsonEscape(note).c_str(), h.timing.buildMs, sum(h.timing.sliceMs), h.timing.wallMs);
    std::ofstream(dir + "/hw-pt.json", std::ios::binary) << b;
    std::fputs(b, stdout);
    return 0;
}
}  // namespace

int hwPtCommand(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        std::printf("usage: raw_native_cli hw-pt contract [--quick] [--out FILE]\n"
                    "       raw_native_cli hw-pt render|time [--scene S] [--width W] [--height H] [--spp N] [--seed S] [--compute] [--out DIR|FILE]\n");
        return 2;
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) { std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    if (a.mode == "contract") {
        const HwPtReport r = hwPtContract(*dev, a.quick);
        std::fputs(r.json.c_str(), stdout);
        if (!a.out.empty()) std::ofstream(a.out, std::ios::binary) << r.json;
        return r.pass ? 0 : 1;
    }
    rt::PtScene s;
    rt::PtCamera cam;
    if (!load(a.scene, s, cam)) { std::printf("hw-pt: unknown scene %s\n", a.scene.c_str()); return 2; }
    a.d.camera = a.d.previous = cam;
    return a.mode == "time" ? timeBoth(*dev, s, a) : render(*dev, s, a);
}

}  // namespace raw
