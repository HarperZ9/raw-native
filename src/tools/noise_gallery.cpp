// The noise and dither goldens: see raw/tools/noise_gallery.hpp.
#include "raw/tools/noise_gallery.hpp"
#include "raw/renderer/dither.hpp"
#include "raw/renderer/noise.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
namespace raw {
namespace {
using namespace raw::noise;
GreyImage make(const std::string& name, int s, float lo, float hi, const std::function<float(int, int)>& f) {
    GreyImage g; g.name = name; g.w = g.h = s; g.lo = lo; g.hi = hi; g.px.resize(std::size_t(s) * s);
    for (int y = 0; y < s; ++y) for (int x = 0; x < s; ++x) g.px[std::size_t(y) * s + x] = f(x, y);
    return g;
}
}  // namespace

std::vector<GreyImage> noiseGoldens(int s) {
    const float k = 1.0f / 16.0f, k2 = 1.0f / 32.0f;
    std::vector<GreyImage> v;
    v.push_back(make("white", s, 0, 1, [](int x, int y) { return white(x, y, 0, 7u); }));
    v.push_back(make("value2", s, 0, 1, [&](int x, int y) { return value2(x * k, y * k, 7u); }));
    v.push_back(make("value3", s, 0, 1, [&](int x, int y) { return value3(x * k, y * k, 0.37f, 7u); }));
    v.push_back(make("perlin2", s, -1, 1, [&](int x, int y) { return perlin2(x * k, y * k, 7u); }));
    v.push_back(make("perlin3", s, -1, 1, [&](int x, int y) { return perlin3(x * k, y * k, 0.37f, 7u); }));
    v.push_back(make("simplex2", s, -1, 1, [&](int x, int y) { return simplex2(x * k, y * k, 7u); }));
    v.push_back(make("simplex3", s, -1, 1, [&](int x, int y) { return simplex3(x * k, y * k, 0.37f, 7u); }));
    v.push_back(make("worley2_f1", s, 0, 1, [&](int x, int y) { return worley2(x * k, y * k, 7u).f1; }));
    v.push_back(make("worley2_f2_minus_f1", s, 0, 0.6f, [&](int x, int y) { const Cell c = worley2(x * k, y * k, 7u); return c.f2 - c.f1; }));
    v.push_back(make("worley3_f1", s, 0, 1, [&](int x, int y) { return worley3(x * k, y * k, 0.37f, 7u).f1; }));
    v.push_back(make("fbm_perlin", s, -0.8f, 0.8f, [&](int x, int y) { return fbm(Base::Perlin, x * k2, y * k2, 0.5f, 7u); }));
    v.push_back(make("ridged_simplex", s, 0, 1, [&](int x, int y) { return ridged(Base::Simplex, x * k2, y * k2, 0.5f, 7u); }));
    v.push_back(make("billow_perlin", s, -1, 0.6f, [&](int x, int y) { return billow(Base::Perlin, x * k2, y * k2, 0.5f, 7u); }));
    v.push_back(make("hybrid_simplex", s, 0, 2.5f, [&](int x, int y) { return hybrid(Base::Simplex, x * k2, y * k2, 0.5f, 7u); }));
    v.push_back(make("warp_fbm", s, -0.8f, 0.8f, [&](int x, int y) { const Warped w = warp(x * k2, y * k2, 0.5f, 7u, 1.2f); return fbm(Base::Simplex, w.x, w.y, w.z, 9u); }));
    v.push_back(make("blue", s, 0, 1, [](int x, int y) { return blue(x, y); }));
    // Recipes (the author's references, section 5; ideas only).
    v.push_back(make("recipe_islands", s, 0, 1, [&](int x, int y) {      // fBm + ridged, coast dithered by blue noise
        const float h = 0.6f * fbm(Base::Simplex, x * k2, y * k2, 0.2f, 11u) + 0.4f * ridged(Base::Simplex, x * k2, y * k2, 0.2f, 12u);
        const float coast = 0.18f + 0.06f * (blue(x, y) - 0.5f);
        return h > coast ? 0.45f + 0.55f * (h - coast) / (1.0f - coast) : 0.15f + 0.25f * std::max(0.0f, h + 0.5f);
    }));
    v.push_back(make("recipe_planet", s, 0, 1, [&](int x, int y) {       // 3D simplex + warp + ridged on a sphere
        const float px = (x + 0.5f) / s * 2.0f - 1.0f, py = 1.0f - (y + 0.5f) / s * 2.0f, r2 = px * px + py * py;
        if (r2 >= 0.92f) return 0.0f;
        const float pz = std::sqrt(0.92f - r2), sc = 2.5f;
        const Warped w = warp(px * sc, py * sc, pz * sc, 21u, 0.6f);
        const float shade = 0.35f + 0.65f * std::max(0.0f, (-0.4f * px + 0.5f * py + 0.77f * pz) / 0.96f);
        return shade * (0.25f + 0.75f * ridged(Base::Simplex, w.x, w.y, w.z, 22u));
    }));
    v.push_back(make("recipe_warp_ridged_white", s, 0, 1, [&](int x, int y) {
        const Warped w = warp(x * k2, y * k2, 0.7f, 31u, 1.5f);
        return 0.9f * ridged(Base::Perlin, w.x, w.y, w.z, 32u) + 0.1f * white(x, y, 0, 33u);
    }));
    v.push_back(make("recipe_ridged_fbm_billow", s, -0.4f, 0.8f, [&](int x, int y) {
        return 0.5f * ridged(Base::Simplex, x * k2, y * k2, 0.9f, 41u) + 0.3f * fbm(Base::Perlin, x * k2, y * k2, 0.9f, 42u) + 0.2f * billow(Base::Perlin, x * k2, y * k2, 0.9f, 43u);
    }));
    return v;
}

GreyImage ditherRamp() {
    GreyImage g; g.name = "ramp"; g.w = 256; g.h = 64; g.px.resize(256 * 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 256; ++x) g.px[std::size_t(y) * 256 + x] = (float(x) + 0.5f) / 256.0f;
    return g;
}
GreyImage ditherCard() {
    GreyImage g; g.name = "card"; g.w = g.h = 128; g.px.resize(128 * 128);
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        float v = y < 64 ? (float(x) + 0.5f) / 128.0f : (float(y - 64) + 0.5f) / 64.0f;   // horizontal then vertical ramp
        const float dx = float(x) - 40.0f, dy = float(y) - 40.0f, ex = float(x) - 92.0f, ey = float(y) - 92.0f;
        if (dx * dx + dy * dy < 400.0f) v = 0.25f;                                           // two discs of fixed tone
        if (ex * ex + ey * ey < 576.0f) v = 0.8f;
        g.px[std::size_t(y) * 128 + x] = v;
    }
    return g;
}
std::vector<GreyImage> ditherGoldens() {
    using dither::Method;
    std::vector<GreyImage> out;
    for (const GreyImage& in : {ditherRamp(), ditherCard()})
        for (Method m : {Method::Bayer2, Method::Bayer4, Method::Bayer8, Method::FloydSteinberg, Method::Atkinson, Method::Stucki, Method::BlueNoise, Method::LineFill, Method::ContourFill}) {
            GreyImage g = in; g.name = std::string(dither::name(m)) + "_" + in.name; g.px = dither::apply(m, in.px, in.w, in.h);
            out.push_back(g);
        }
    return out;
}

int noiseGalleryCommand(int argc, char** argv) {
    std::string out = "noise-gallery";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli noise-gallery [--out DIR]\n"); return 2; }
    }
    std::filesystem::create_directories(out);
    std::vector<GreyImage> all = noiseGoldens();
    for (GreyImage& g : ditherGoldens()) all.push_back(g);
    for (const GreyImage& g : all) {
        std::vector<std::uint8_t> rgb(std::size_t(g.w) * g.h * 3);
        for (std::size_t i = 0; i < g.px.size(); ++i) {
            const float t = std::clamp((g.px[i] - g.lo) / (g.hi - g.lo), 0.0f, 1.0f);
            rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = std::uint8_t(std::lround(t * 255.0f));
        }
        if (!writePng(out + "/" + g.name + ".png", g.w, g.h, rgb)) { std::printf("noise-gallery: cannot write %s\n", out.c_str()); return 2; }
    }
    std::printf("noise-gallery: %zu images in %s\n", all.size(), out.c_str());
    return 0;
}
}  // namespace raw
