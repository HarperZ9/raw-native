// raw_native_cli export-assets: see raw/tools/owned_assets.hpp.
#include "raw/tools/owned_assets.hpp"
#include "raw/core/sha256.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace raw {
namespace {
// A quick look, not a check: Lambert under the sun plus ambient, emissive parts in their colour.
void preview(const owned::Asset& a, Camera cam, int size, const std::string& path) {
    Scene s = owned::toScene(a);
    cam.aspect = 1.0f;
    s.camera = cam;
    Buffer<uint16_t> ids;
    RasterOptions ro; ro.perspectiveDepth = true;
    const GBuffer g = rasterize(s, size, size, nullptr, &ids, ro);
    const Vec3 L = normalize(s.lights[0].dir);
    std::vector<uint8_t> rgb(std::size_t(size) * size * 3);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        double c[3] = {0.55, 0.62, 0.72};
        if (g.mask.at(x, y)) {
            const owned::Part& p = a.parts[std::size_t(ids.at(x, y) - 1)];
            const double lit = 0.25 + std::max(0.0, double(-dot(normalize(g.normal.at(x, y)), L)));
            const pbr::Rgb& b = p.material.baseColor;
            const pbr::Rgb& e = p.material.emissive;
            const double base[3] = {b.r, b.g, b.b}, em[3] = {e.r, e.g, e.b};
            for (int k = 0; k < 3; ++k) c[k] = (p.material.metallic > 0.5 ? 0.6 : 1.0) * base[k] * lit + em[k];
        }
        for (int k = 0; k < 3; ++k) {
            const double v = std::clamp(c[k], 0.0, 1.0), srgb = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
            rgb[(std::size_t(y) * size + x) * 3 + std::size_t(k)] = uint8_t(std::lround(srgb * 255.0));
        }
    }
    writePng(path, size, size, rgb);
}
}  // namespace

int exportAssetsCommand(int argc, char** argv) {
    std::string out = "owned-assets";
    bool previews = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--preview")) previews = true;
        else { std::printf("usage: raw_native_cli export-assets [--out DIR] [--preview]\n"); return 2; }
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    for (const owned::Asset& a : {owned::hero(), owned::hall()}) {
        std::vector<uint8_t> bin;
        const std::string js = owned::exportGltf(a, bin);
        std::ofstream(out + "/" + a.name + ".gltf", std::ios::binary) << js;
        std::ofstream(out + "/" + a.name + ".bin", std::ios::binary).write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size()));
        std::printf("%s: %s/%s.gltf (%zu bytes, sha256 %s) and .bin (%zu bytes, sha256 %s)\n", a.name.c_str(), out.c_str(), a.name.c_str(), js.size(),
                    sha256Hex(js).c_str(), bin.size(), sha256Hex(bin.data(), bin.size()).c_str());
    }
    if (previews) {
        const owned::Asset h = owned::hero(), hall = owned::hall();
        preview(h, owned::toScene(h).camera, 512, out + "/raw-hero-preview.png");
        Camera f = owned::toScene(h).camera;
        f.eye = {f.center.x + 0.12f, f.center.y + 0.08f, f.center.z + 0.5f};
        preview(h, f, 512, out + "/raw-hero-front.png");
        Camera c;
        c.eye = {-15.5f, 2.0f, 0.0f}; c.center = {10.0f, 5.5f, 0.0f}; c.fovy = 1.1f;
        preview(hall, c, 512, out + "/raw-hall-nave.png");
        c.eye = {-12.0f, 9.0f, 6.6f}; c.center = {4.0f, 5.0f, -4.0f};
        preview(hall, c, 512, out + "/raw-hall-gallery.png");
    }
    return 0;
}
}  // namespace raw
