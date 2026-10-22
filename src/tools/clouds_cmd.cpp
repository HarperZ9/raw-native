// raw_native_cli clouds: the cloud module's goldens and the blob review as images
// (evidence/p14-clouds-bounds.json v3 and v5).
//   raw_native_cli clouds [--out DIR] [--width W] [--height H]
#include "raw/renderer/volume.hpp"
#include "raw/tools/image_out.hpp"
#include "raw/tools/noise_gallery.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
namespace raw {

int cloudsCommand(int argc, char** argv) {
    std::string out = "clouds";
    int w = 384, h = 216;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) h = std::atoi(argv[++i]);
        else { std::printf("usage: raw_native_cli clouds [--out DIR] [--width W] [--height H]\n"); return 2; }
    }
    if (w < 8 || h < 8) { std::printf("clouds: sizes at least 8\n"); return 2; }
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
    for (volume::Scene s : {volume::Scene::Cumulus, volume::Scene::Towering, volume::Scene::Deck, volume::Scene::SmokeSunset}) {
        volume::Cloud c; volume::Lighting l; volume::Camera cam; volume::scene(s, c, l, cam);
        shoot(volume::name(s), c, l, cam, s == volume::Scene::SmokeSunset ? 10.5 : 14.0);
        if (s == volume::Scene::Cumulus) { c.noiseBlob = true; shoot("review_noise_blob_control", c, l, cam, 14.0); }
    }
    return 0;
}
}  // namespace raw
