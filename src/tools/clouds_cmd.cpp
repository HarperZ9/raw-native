// raw_native_cli clouds: the cloud module's goldens and the blob review as images
// (evidence/p14-clouds-bounds.json v3 and v5).
//   raw_native_cli clouds [--out DIR] [--width W] [--height H]
// One shot of one scene, with its sun, sky and camera overridden (for stills and frame sequences):
//   raw_native_cli clouds --scene cumulus|towering_cumulus|deck_from_above|smoke_plume_sunset
//       [--sun-elev DEG] [--sun-az DEG] [--turbidity T] [--eye x,y,z] [--target x,y,z] [--fovy RAD]
//       [--ev EV100] [--step M] [--name STEM] [--out DIR] [--width W] [--height H]
// With no --scene the command writes the goldens exactly as before.
#include "raw/renderer/volume.hpp"
#include "raw/tools/image_out.hpp"
#include "raw/tools/noise_gallery.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
namespace raw {
namespace {
bool d3Arg(const char* s, lighting::D3& v) { return std::sscanf(s, "%lf,%lf,%lf", &v.x, &v.y, &v.z) == 3; }
struct Shot {
    std::string scene, stem;
    double sunElev{-1}, sunAz{0}, turbidity{-1}, fovy{-1}, ev{-100}, step{-1};
    bool sunAzSet{false}, eyeSet{false}, targetSet{false};
    lighting::D3 eye, target;
    bool any() const { return sunElev >= 0 || sunAzSet || turbidity >= 0 || fovy >= 0 || ev > -100 || step >= 0 || eyeSet || targetSet || !stem.empty(); }
};
}  // namespace

int cloudsCommand(int argc, char** argv) {
    std::string out = "clouds";
    int w = 384, h = 216;
    Shot o;
    bool ok = true;
    for (int i = 1; i < argc && ok; ++i) {
        const bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--out") && more) out = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && more) w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && more) h = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scene") && more) o.scene = argv[++i];
        else if (!std::strcmp(argv[i], "--name") && more) o.stem = argv[++i];
        else if (!std::strcmp(argv[i], "--sun-elev") && more) o.sunElev = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--sun-az") && more) { o.sunAz = std::atof(argv[++i]); o.sunAzSet = true; }
        else if (!std::strcmp(argv[i], "--turbidity") && more) o.turbidity = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--fovy") && more) o.fovy = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--ev") && more) o.ev = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--step") && more) o.step = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--eye") && more) ok = o.eyeSet = d3Arg(argv[++i], o.eye);
        else if (!std::strcmp(argv[i], "--target") && more) ok = o.targetSet = d3Arg(argv[++i], o.target);
        else ok = false;
    }
    if (!ok) {
        std::printf("usage: raw_native_cli clouds [--out DIR] [--width W] [--height H]\n"
                    "       raw_native_cli clouds --scene NAME [--sun-elev DEG] [--sun-az DEG] [--turbidity T] [--eye x,y,z] [--target x,y,z]\n"
                    "                             [--fovy RAD] [--ev EV100] [--step M] [--name STEM] [--out DIR] [--width W] [--height H]\n");
        return 2;
    }
    if (w < 8 || h < 8) { std::printf("clouds: sizes at least 8\n"); return 2; }
    if (o.scene.empty() && o.any()) { std::printf("clouds: the overrides need --scene\n"); return 2; }
    if ((o.sunElev > 90) || (o.turbidity >= 0 && (o.turbidity < 1.7 || o.turbidity > 10)) || (o.fovy >= 0 && (o.fovy < 0.01 || o.fovy > 3.1)) ||
        (o.step >= 0 && o.step < 0.002)) {
        std::printf("clouds: sun elevation 0..90 degrees, turbidity 1.7..10, fovy 0.01..3.1, step at least 0.002\n");
        return 2;
    }
    std::filesystem::create_directories(out);
    volume::MarchDesc g; g.powder = true; g.ambient = true; g.octaves = 3; g.step = 0.04; g.sunSteps = 24;
    const auto shoot = [&](const std::string& name, const volume::Cloud& c, const volume::Lighting& l, const volume::Camera& cam, double ev) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<lighting::Rgb> img = volume::march(c, l, cam, w, h, g);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const double ex = 1.0 / (1.2 * std::pow(2.0, ev));
        std::vector<lighting::Rgb> e(img.size());
        for (std::size_t i = 0; i < img.size(); ++i) e[i] = {img[i].r * ex, img[i].g * ex, img[i].b * ex};
        writeImages(out + "/" + name, w, h, e);
        std::printf("clouds: %s %dx%d in %.0f ms\n", name.c_str(), w, h, ms);
    };
    const volume::Scene all[] = {volume::Scene::Cumulus, volume::Scene::Towering, volume::Scene::Deck, volume::Scene::SmokeSunset};
    if (!o.scene.empty()) {
        for (volume::Scene s : all) {
            if (o.scene != volume::name(s)) continue;
            volume::Cloud c; volume::Lighting l; volume::Camera cam; volume::scene(s, c, l, cam);
            if (o.sunElev >= 0 || o.sunAzSet) {
                // Keep the scene's own elevation or azimuth for whichever is not given.
                constexpr double kDeg = 3.141592653589793 / 180.0;
                const double el = o.sunElev >= 0 ? o.sunElev * kDeg : std::asin(std::clamp(l.sky.sun.y, -1.0, 1.0));
                const double az = o.sunAzSet ? o.sunAz * kDeg : std::atan2(l.sky.sun.z, l.sky.sun.x);
                l.sky.sun = {std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)};
            }
            if (o.turbidity >= 0) l.sky.turbidity = o.turbidity;
            if (o.eyeSet) cam.eye = o.eye;
            if (o.targetSet) cam.target = o.target;
            if (o.fovy >= 0) cam.fovy = o.fovy;
            if (o.step >= 0) g.step = o.step;
            const double ev = o.ev > -100 ? o.ev : (s == volume::Scene::SmokeSunset ? 10.5 : 14.0);
            shoot(o.stem.empty() ? std::string(volume::name(s)) : o.stem, c, l, cam, ev);
            return 0;
        }
        std::printf("clouds: unknown scene %s\n", o.scene.c_str());
        return 2;
    }
    for (volume::Scene s : all) {
        volume::Cloud c; volume::Lighting l; volume::Camera cam; volume::scene(s, c, l, cam);
        shoot(volume::name(s), c, l, cam, s == volume::Scene::SmokeSunset ? 10.5 : 14.0);
        if (s == volume::Scene::Cumulus) { c.noiseBlob = true; shoot("review_noise_blob_control", c, l, cam, 14.0); }
    }
    return 0;
}
}  // namespace raw
