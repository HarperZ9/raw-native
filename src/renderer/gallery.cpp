// The material gallery: see raw/renderer/gallery.hpp.
#include "raw/renderer/gallery.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gallery {
namespace {
using lighting::D3;
D3 unit(D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return {a.x / l, a.y / l, a.z / l}; }
constexpr double kRadius = 0.47, kSpacing = 1.05, kDepth = 11.0, kFovy = 0.66;
D3 centre(int row, int col) { return {(col - 3) * kSpacing, (3 - row) * kSpacing, -kDepth}; }
std::vector<lighting::Light> lights() {
    using lighting::Light; using lighting::LightType;
    const D3 mid = centre(3, 3);
    Light key; key.type = LightType::Spot; key.position = {-5, 5, -4};
    key.direction = unit({mid.x - key.position.x, mid.y - key.position.y, mid.z - key.position.z});
    key.intensity = 60000; key.range = 40; key.innerCone = 0.25; key.outerCone = 0.5; key.color = {1.0, 0.96, 0.9};
    Light rim; rim.type = LightType::Point; rim.position = {5, -2, -14}; rim.intensity = 20000; rim.range = 25; rim.color = {0.9, 0.95, 1.0};
    Light fill; fill.type = LightType::Directional; fill.direction = unit({0.8, -0.3, -0.5}); fill.intensity = 400; fill.color = {0.6, 0.7, 1.0};
    return {key, rim, fill};
}
}  // namespace

pbr::Material sphereMaterial(int row, int col, std::string* label) {
    pbr::Material m;
    const double t = col / 6.0;
    const char* name = "";
    switch (row) {
    case 0: m.baseColor = {1.0, 0.766, 0.336}; m.metallic = 1; m.roughness = 0.05 + 0.95 * t; name = "gold, roughness"; break;
    case 1: m.baseColor = {0.75, 0.06, 0.05}; m.metallic = 0; m.roughness = 0.05 + 0.95 * t; name = "red plastic, roughness"; break;
    case 2: m.baseColor = {0.55, 0.02, 0.04}; m.metallic = 0.6; m.roughness = 0.45; m.clearcoat = 1; m.clearcoatRoughness = 0.03 + 0.6 * t;
            name = "car paint, clearcoat roughness"; break;
    case 3: m.baseColor = {0.05, 0.05, 0.25}; m.metallic = 0; m.roughness = 0.8; m.sheenColor = {0.9, 0.55, 0.75}; m.sheenRoughness = 0.1 + 0.9 * t;
            name = "velvet, sheen roughness"; break;
    case 4: m.baseColor = {0.8, 0.8, 0.82}; m.metallic = 1; m.roughness = 0.35; m.anisotropy = t; name = "brushed steel, anisotropy"; break;
    case 5: m.baseColor = {0.04, 0.04, 0.05}; m.metallic = 1; m.roughness = 0.15; m.iridescence = 1; m.iridescenceIor = 1.33;
            m.iridescenceThickness = 100 + 900 * t; name = "thin film, iridescence thickness"; break;
    default: m.baseColor = {0.98, 0.98, 0.98}; m.metallic = 0; m.transmission = 1; m.roughness = 0.02 + 0.6 * t;
             m.volume = col >= 4; m.thickness = 0.5; m.attenuationDistance = 1.0; m.attenuationColor = {0.7, 0.9, 0.85};
             name = "glass, roughness (thin, then volume)"; break;
    }
    if (label) *label = name;
    return m;
}

Gallery build(int width, int height, int spp) {
    Gallery g;
    g.width = width; g.height = height; g.spp = std::max(1, spp);
    g.grid.fovy = kFovy; g.grid.aspect = double(width) / height; g.grid.nearZ = 0.1; g.grid.farZ = 200.0;
    g.lights = lights();
    g.env = lighting::cubeFrom(lighting::proceduralSky, 32, 6);
    g.sh = lighting::shProject(g.env);
    g.exposure = lighting::exposure(kEv100);
    g.background.assign(std::size_t(width) * height, {});
    g.hits.assign(std::size_t(width) * height, 0);
    const double th = std::tan(0.5 * kFovy), inv = 1.0 / (g.spp * g.spp);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int sy = 0; sy < g.spp; ++sy) for (int sx = 0; sx < g.spp; ++sx) {
        const double u = (x + (sx + 0.5) / g.spp) / width * 2.0 - 1.0, v = 1.0 - (y + (sy + 0.5) / g.spp) / height * 2.0;
        const D3 dir = unit({u * th * g.grid.aspect, v * th, -1.0});
        const int pix = y * width + x;
        const int col = std::clamp(int(std::floor(dir.x / -dir.z * kDepth / kSpacing + 3.5)), 0, kCols - 1);
        const int row = std::clamp(int(std::floor(3.5 - dir.y / -dir.z * kDepth / kSpacing)), 0, kRows - 1);
        bool hit = false;
        for (int dr = -1; dr <= 1 && !hit; ++dr) for (int dc = -1; dc <= 1 && !hit; ++dc) {
            const int r = row + dr, c = col + dc;
            if (r < 0 || r >= kRows || c < 0 || c >= kCols) continue;
            const D3 o = centre(r, c);
            const double b = dir.x * o.x + dir.y * o.y + dir.z * o.z, disc = b * b - (o.x * o.x + o.y * o.y + o.z * o.z - kRadius * kRadius);
            if (disc < 0.0) continue;
            const double tt = b - std::sqrt(disc);
            lighting::Sample s;
            s.p = {dir.x * tt, dir.y * tt, dir.z * tt};
            s.n = unit({s.p.x - o.x, s.p.y - o.y, s.p.z - o.z});
            s.t = unit({-s.n.z, 0.0, s.n.x});              // along the sphere's latitude
            if (!std::isfinite(s.t.x)) s.t = {1, 0, 0};
            s.v = {-dir.x, -dir.y, -dir.z};
            s.m = sphereMaterial(r, c);
            g.samples.push_back(s); g.pixel.push_back(pix); ++g.hits[std::size_t(pix)];
            hit = true;
        }
        if (!hit) {
            const Rgb L = g.env.sample(0, dir);
            Rgb& bg = g.background[std::size_t(pix)];
            bg = {bg.r + L.r * g.exposure * inv, bg.g + L.g * g.exposure * inv, bg.b + L.b * g.exposure * inv};
        }
    }
    return g;
}

std::vector<Rgb> resolve(const Gallery& g, const std::vector<Rgb>& perSample) {
    std::vector<Rgb> img = g.background;
    const double inv = 1.0 / (g.spp * g.spp);
    for (std::size_t k = 0; k < perSample.size(); ++k) {
        Rgb& p = img[std::size_t(g.pixel[k])];
        p = {p.r + perSample[k].r * inv, p.g + perSample[k].g * inv, p.b + perSample[k].b * inv};
    }
    return img;
}

std::vector<Rgb> shadeCpu(const Gallery& g, const pbr::Tables& t, const lighting::Cube& prefiltered, int threads) {
    std::vector<Rgb> out(g.samples.size());
    std::vector<int> all;
    for (std::size_t k = 0; k < g.lights.size(); ++k) all.push_back(int(k));
    const int rows = int((g.samples.size() + 1023) / 1024);
    parallelRows(rows, threads, [&](int r) {
        for (std::size_t k = std::size_t(r) * 1024; k < std::min(g.samples.size(), std::size_t(r + 1) * 1024); ++k) {
            const lighting::Sample& s = g.samples[k];
            const int c = g.grid.clusterOf(s.p);
            const std::vector<int> list = c >= 0 ? all : std::vector<int>{};
            const Rgb a = lighting::shadePunctual(s, t, g.lights, list), b = lighting::shadeIbl(s, t, prefiltered, g.sh);
            out[k] = {(a.r + b.r) * g.exposure, (a.g + b.g) * g.exposure, (a.b + b.b) * g.exposure};
        }
    });
    return out;
}

}  // namespace raw::gallery
