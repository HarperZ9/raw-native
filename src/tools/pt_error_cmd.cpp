// raw_native_cli pt-error: the rasterised frame's error against a path-traced reference
// (ROADMAP M3 criterion 1; the measure, its controls and the bounds are in
// evidence/m3-verified-scenes-bounds.json, written before this command existed).
//
//   raw_native_cli pt-error --raster DIR [scene flags as for a render] [--spp N] [--bounces B]
//                           [--no-sun-pi] [--json FILE]
//
// DIR is a render's output directory (frame_hdr.pfm and mask.pgm, e.g. a --gpu run's). The scene
// flags (--model, --owned-scene, --width, --height, --eye, ...) must be the ones that render used.
// The path tracer renders the same scene through rt::ptFromScene at the pixel centres; the
// comparison is over pixels both cover, in luminance. Exit 0 when the measure was taken, 2 on bad
// input; it judges nothing, the gate does (tests/gpu/m3_scenes_gate.py).
#include "raw/renderer/rt_pt_convert.hpp"
#include "raw/tools/cli_params.hpp"
#include "raw/tools/model_scene.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
namespace raw {
namespace {
// Header of a netpbm or PFM file: magic, width, height, then one number (max or scale) and one byte.
bool header(std::ifstream& f, std::string& magic, int& w, int& h){
    double scale = 0;
    f >> magic >> w >> h >> scale;
    f.get();
    return bool(f) && w > 0 && h > 0;
}
// frame_hdr.pfm (rows bottom to top, little endian), into rows top to bottom.
bool readPfm3(const std::string& path, int& w, int& h, std::vector<float>& rgb){
    std::ifstream f(path, std::ios::binary);
    std::string m;
    if (!header(f, m, w, h) || m != "PF") return false;
    rgb.assign(std::size_t(w) * h * 3, 0.0f);
    for (int y = h - 1; y >= 0; --y) f.read(reinterpret_cast<char*>(rgb.data() + std::size_t(y) * w * 3), std::streamsize(w) * 12);
    return bool(f);
}
bool readPgm(const std::string& path, int& w, int& h, std::vector<unsigned char>& px){
    std::ifstream f(path, std::ios::binary);
    std::string m;
    if (!header(f, m, w, h) || m != "P5") return false;
    px.resize(std::size_t(w) * h);
    f.read(reinterpret_cast<char*>(px.data()), std::streamsize(px.size()));
    return bool(f);
}
double lum(const float* c){ return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
}  // namespace

int ptErrorCommand(int argc, char** argv){
    std::string raster, json;
    int spp = 128, bounces = 8;
    bool sunPi = true;
    std::vector<const char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i){
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--raster" && more) raster = argv[++i];
        else if (a == "--json" && more) json = argv[++i];
        else if (a == "--spp" && more) spp = std::atoi(argv[++i]);
        else if (a == "--bounces" && more) bounces = std::atoi(argv[++i]);
        else if (a == "--no-sun-pi") sunPi = false;
        else rest.push_back(argv[i]);
    }
    std::string err;
    const auto p = parseArgs(int(rest.size()), rest.data(), err);
    if (!p || raster.empty() || spp < 1 || bounces < 1){
        std::printf("usage: raw_native_cli pt-error --raster DIR [scene flags] [--spp N] [--bounces B] [--no-sun-pi] [--json FILE]%s%s\n",
                    err.empty() ? "" : "\n  ", err.c_str());
        return 2;
    }
    int w = 0, h = 0, mw = 0, mh = 0;
    std::vector<float> hdr; std::vector<unsigned char> mask;
    if (!readPfm3(raster + "/frame_hdr.pfm", w, h, hdr) || !readPgm(raster + "/mask.pgm", mw, mh, mask) || mw != w || mh != h){
        std::printf("pt-error: cannot read %s/frame_hdr.pfm and mask.pgm of one size\n", raster.c_str()); return 2; }
    if (w != p->width || h != p->height){ std::printf("pt-error: the raster is %dx%d, the flags say %dx%d\n", w, h, p->width, p->height); return 2; }

    Scene s = sceneFromParams(*p, nullptr);
    s.camera = cameraFromParams(*p);
    const auto t0 = std::chrono::steady_clock::now();
    const rt::PtScene pts = rt::ptFromScene(s, 0.2f, sunPi);
    rt::PathTraceDesc d;
    d.width = w; d.height = h; d.spp = std::uint32_t(spp); d.maxBounces = std::uint32_t(bounces); d.seed = 1;
    d.camera = d.previous = rt::ptCameraOf(s.camera); d.jitter = false;
    const rt::PathTraceOutput o = rt::pathTraceCpu(pts, d, p->threads);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!o.error.empty()){ std::printf("pt-error: %s\n", o.error.c_str()); return 2; }

    long both = 0, agree = 0;
    double sr = 0, sp = 0, sd2 = 0, sd = 0, sv = 0;
    for (int i = 0; i < w * h; ++i){
        const bool rc = mask[std::size_t(i)] != 0, pc = o.triangle[std::size_t(i)] >= 0;
        agree += rc == pc;
        if (!rc || !pc) continue;
        ++both;
        const double yr = lum(hdr.data() + 3 * std::size_t(i)), yp = lum(o.radiance.data() + 3 * std::size_t(i));
        sr += yr; sp += yp; sd += yr - yp; sd2 += (yr - yp) * (yr - yp); sv += o.variance[std::size_t(i)];
    }
    const double n = both ? double(both) : 1.0, meanP = sp / n;
    const double rel = meanP > 0 ? std::sqrt(sd2 / n) / meanP : 0, bias = meanP > 0 ? (sd / n) / meanP : 0;
    const double noise = meanP > 0 ? std::sqrt(sv / n) / meanP : 0, agreeFrac = double(agree) / double(w * h);
    char buf[1024];
    std::snprintf(buf, sizeof buf,
        "{\n \"schema\": \"raw-native.pt-error/1\",\n \"scene\": \"%s\",\n \"width\": %d, \"height\": %d,\n"
        " \"spp\": %d, \"bounces\": %d, \"sun_pi\": %s, \"ambient\": 0.2,\n \"pixels_both_cover\": %ld,\n"
        " \"coverage_agreement\": %.6f,\n \"mean_luminance_raster\": %.6g, \"mean_luminance_pt\": %.6g,\n"
        " \"relative_rmse\": %.6g, \"bias\": %.6g, \"noise_floor\": %.6g,\n \"pt_ms\": %.1f\n}\n",
        p->ownedScene.empty() ? (p->model.empty() ? "test-scene" : p->model.substr(p->model.find_last_of("/\\") + 1).c_str()) : p->ownedScene.c_str(),
        w, h, spp, bounces, sunPi ? "true" : "false", both, agreeFrac, sr / n, meanP, rel, bias, noise, ms);
    if (!json.empty()) std::ofstream(json, std::ios::binary) << buf;
    std::printf("%s", buf);
    return 0;
}
}  // namespace raw
