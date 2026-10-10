// raw_native_cli rt-pathtrace: see raw/tools/pt_cmd.hpp.
#include "raw/tools/pt_cmd.hpp"
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/tools/image_out.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
// A float PFM (Pf: one channel, PF: three), rows bottom to top, little-endian.
bool writePfm(const std::string& path, int w, int h, int ch, const std::vector<float>& v) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << (ch == 1 ? "Pf" : "PF") << "\n" << w << " " << h << "\n-1.0\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&v[std::size_t(y) * std::size_t(w) * std::size_t(ch)]), std::streamsize(std::size_t(w) * std::size_t(ch) * 4));
    return bool(f);
}
bool vec3Arg(const char* s, Vec3& v) { return std::sscanf(s, "%f,%f,%f", &v.x, &v.y, &v.z) == 3; }
struct Args {
    std::string scene{"retro_room"}, out{"pathtrace"};
    rt::PathTraceDesc d;
    bool cpu{false}, prevEye{false}, prevTarget{false};
    Vec3 pe, pt;
};
bool parse(int argc, char** argv, Args& a) {
    a.d.width = 320; a.d.height = 240; a.d.spp = 64;
    for (int i = 1; i < argc; ++i) {
        const bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--scene") && more) a.scene = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && more) a.d.width = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && more) a.d.height = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--spp") && more) a.d.spp = std::uint32_t(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--seed") && more) a.d.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--bounces") && more) a.d.maxBounces = std::uint32_t(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--out") && more) a.out = argv[++i];
        else if (!std::strcmp(argv[i], "--cpu")) a.cpu = true;
        else if (!std::strcmp(argv[i], "--prev-eye") && more) a.prevEye = vec3Arg(argv[++i], a.pe);
        else if (!std::strcmp(argv[i], "--prev-target") && more) a.prevTarget = vec3Arg(argv[++i], a.pt);
        else return false;
    }
    return a.d.width >= 8 && a.d.width <= 4096 && a.d.height >= 8 && a.d.height <= 4096 && a.d.spp >= 1 && a.d.spp <= 65536 && a.d.maxBounces <= 64;
}
}  // namespace

int ptCommand(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        std::printf("usage: raw_native_cli rt-pathtrace [--scene retro_room|iso_street|dense_block] [--width W] [--height H] [--spp N]\n"
                    "         [--seed S] [--bounces B] [--out DIR] [--cpu] [--prev-eye x,y,z] [--prev-target x,y,z]\n");
        return 2;
    }
    rt::PtScene s;
    swr::Scene base;
    if (a.scene == "retro_room") { s = rt::ptRetroRoom(); base = swr::retroRoom(); }
    else if (a.scene == "iso_street") { s = rt::ptIsoStreet(); base = swr::isoStreet(); }
    else if (a.scene == "dense_block") { s = rt::ptDenseBlock(); base = rt::denseBlock(); }
    else { std::printf("rt-pathtrace: unknown scene %s\n", a.scene.c_str()); return 2; }
    a.d.camera = a.d.previous = rt::cameraOf(base);
    if (a.prevEye) a.d.previous.eye = a.pe;
    if (a.prevTarget) a.d.previous.target = a.pt;
    std::error_code ec;
    std::filesystem::create_directories(a.out, ec);
    if (ec || !std::filesystem::is_directory(a.out)) { std::printf("rt-pathtrace: cannot create %s\n", a.out.c_str()); return 2; }
    std::string why;
    rhi::Device* dev = a.cpu ? nullptr : rhi::device(why);
    const auto t0 = std::chrono::steady_clock::now();
    const rt::PathTraceOutput o = dev ? gpu_check::pathTraceGpu(*dev, s, a.d) : rt::pathTraceCpu(s, a.d);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!o.error.empty()) { std::printf("rt-pathtrace: %s\n", o.error.c_str()); return 2; }
    const int W = o.width, H = o.height;
    std::vector<lighting::Rgb> img(std::size_t(W) * std::size_t(H));
    for (std::size_t p = 0; p < img.size(); ++p) img[p] = {o.radiance[p * 3], o.radiance[p * 3 + 1], o.radiance[p * 3 + 2]};
    std::vector<float> motion3(img.size() * 3, 0.0f);
    for (std::size_t p = 0; p < img.size(); ++p) { motion3[p * 3] = o.motion[p * 2]; motion3[p * 3 + 1] = o.motion[p * 2 + 1]; }
    bool ok = writeImages(a.out + "/radiance", W, H, img) && writePfm(a.out + "/albedo.pfm", W, H, 3, o.albedo) &&
              writePfm(a.out + "/normal.pfm", W, H, 3, o.normal) && writePfm(a.out + "/depth.pfm", W, H, 1, o.depth) &&
              writePfm(a.out + "/motion.pfm", W, H, 3, motion3) && writePfm(a.out + "/variance.pfm", W, H, 1, o.variance);
    std::ofstream(a.out + "/triangle.u32", std::ios::binary).write(reinterpret_cast<const char*>(o.triangle.data()), std::streamsize(o.triangle.size() * 4));
    char j[700];
    std::snprintf(j, sizeof j, "{\n \"interface_version\": %d,\n \"scene\": \"%s\",\n \"width\": %d,\n \"height\": %d,\n \"spp\": %u,\n \"seed\": %llu,\n"
                  " \"max_bounces\": %u,\n \"backend\": \"%s\",\n \"adapter\": \"%s\",\n \"ms\": %.1f\n}\n",
                  o.version, a.scene.c_str(), W, H, o.spp, (unsigned long long)a.d.seed, a.d.maxBounces, dev ? dev->backendName() : "cpu",
                  dev ? dev->adapter().description.c_str() : "", ms);
    std::ofstream(a.out + "/pt.json") << j;
    std::printf("%s", j);
    if (!ok) { std::printf("rt-pathtrace: cannot write into %s\n", a.out.c_str()); return 2; }
    return 0;
}

}  // namespace raw
