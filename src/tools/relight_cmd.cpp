// raw_native_cli envmaps and relight (evidence/sky-envmaps-bounds.json).
//
//   raw_native_cli envmaps [--out DIR] [--size N]
//     bakes the four analytic skies and the interior probe (path tracer, inside retro_room), writes
//     each as DIR/<name>.png (six faces in a row, auto-exposed) and prints a JSON array of lock
//     entries: name, recipe, size, SHA-256 of the level-0 float64 data, compiler family.
//   raw_native_cli relight [--out DIR] [--size N]
//     the material gallery and the sphere field under every environment (image lighting only), with
//     their albedo, normal, roughness and depth channels, as PNGs and DIR/index.html.
#include "raw/core/sha256.hpp"
#include "raw/renderer/gallery.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/sky.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
namespace raw {
namespace {
using lighting::Cube; using lighting::Rgb; using lighting::D3;
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
// The interior probe: six path-traced faces from inside retro_room, each camera matching the cube's
// face orientation (a face's u axis is mirrored when the tracer's right-hand axis points the other way).
Cube bakeProbe(int size, int spp, D3 (*viewToWorld)(D3)) {
    rt::PtScene s = rt::ptRetroRoom();
    Cube c; c.size = size; c.levels = 6; c.rgb.assign(c.levelOffset(6), 0.0);
    const auto W = [&](D3 d) { return viewToWorld ? viewToWorld(d) : d; };
    const Vec3 eye{0.0f, 1.6f, -5.0f};
    for (int f = 0; f < 6; ++f) {
        const D3 fw = W(lighting::cube::dirOf(f, 0.5, 0.5)), right = W(lighting::cube::dirOf(f, 1.0, 0.5)), down = W(lighting::cube::dirOf(f, 0.5, 1.0));
        const Vec3 F{float(fw.x), float(fw.y), float(fw.z)}, Up{float(fw.x - down.x), float(fw.y - down.y), float(fw.z - down.z)};
        rt::PathTraceDesc d; d.width = d.height = size; d.spp = std::uint32_t(spp); d.seed = 5; d.jitter = false;
        d.camera = d.previous = {eye, eye + F, Up, 1.5707963f};
        const rt::PathTraceOutput o = rt::pathTraceCpu(s, d, 0);
        const Vec3 sAxis = normalize(cross(F, normalize(Up)));
        const bool mirror = sAxis.x * float(right.x - fw.x) + sAxis.y * float(right.y - fw.y) + sAxis.z * float(right.z - fw.z) < 0.0f;
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            const int sx = mirror ? size - 1 - x : x;
            const float* q = o.radiance.data() + (std::size_t(y) * size + sx) * 3;
            const std::size_t i = ((std::size_t(f) * size + y) * size + x) * 3;
            c.rgb[i] = q[0]; c.rgb[i + 1] = q[1]; c.rgb[i + 2] = q[2];
        }
    }
    return c;
}
struct EnvMap { std::string name, recipe; Cube cube; };
std::vector<EnvMap> allEnvs(int size, D3 (*viewToWorld)(D3)) {
    std::vector<EnvMap> v;
    const char* recipes[4] = {"Preetham turbidity 2.5, sun 70 deg up at azimuth 30; sun disk deposited (sky::sunIlluminance)",
                              "CIE overcast, zenith 8000 nits, ground albedo 0.25",
                              "Preetham turbidity 4, sun 4 deg up at azimuth 200; sun disk deposited",
                              "dim blue gradient, white-noise stars (seed 0x57a7), full moon 0.25 lux at 35 deg up, azimuth 120"};
    for (int k = 0; k < 4; ++k) v.push_back({sky::name(sky::Env(k)), recipes[k], sky::bake(sky::Env(k), size, 6, viewToWorld)});
    v.push_back({"interior_probe", "path tracer v2, retro_room, eye (0, 1.6, -5), 64 spp a texel, seed 5, unjittered", bakeProbe(size, 64, viewToWorld)});
    return v;
}
double autoExposure(const Cube& c) {   // 0.18 over the median texel luminance of level 0
    std::vector<double> l;
    for (std::size_t i = 0; i + 2 < c.levelOffset(1); i += 3) l.push_back(lum({c.rgb[i], c.rgb[i + 1], c.rgb[i + 2]}));
    std::nth_element(l.begin(), l.begin() + std::ptrdiff_t(l.size() / 2), l.end());
    return 0.18 / std::max(l[l.size() / 2], 1e-9);
}
bool writeGrey(const std::string& path, int w, int h, const std::vector<double>& v, double lo, double hi) {
    std::vector<std::uint8_t> rgb(std::size_t(w) * h * 3);
    for (std::size_t i = 0; i < v.size(); ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = std::uint8_t(std::lround(255.0 * std::clamp((v[i] - lo) / (hi - lo), 0.0, 1.0)));
    return writePng(path, w, h, rgb);
}
// The background under a new environment: every sub-sample direction of the camera (the gallery's
// and the field's cameras share the pinhole form), weighted by the pixel's missed share. Exact for
// pixels that miss entirely; edge pixels assume their misses spread evenly over the pixel.
void rebackground(gallery::Gallery& g) {
    const double th = std::tan(0.5 * g.fovy), aspect = double(g.width) / g.height, inv = 1.0 / (g.spp * g.spp);
    for (int y = 0; y < g.height; ++y) for (int x = 0; x < g.width; ++x) {
        const std::size_t p = std::size_t(y) * g.width + x;
        const double miss = 1.0 - g.hits[p] * inv;
        Rgb acc{};
        for (int sy = 0; sy < g.spp; ++sy) for (int sx = 0; sx < g.spp; ++sx) {
            const double u = (x + (sx + 0.5) / g.spp) / g.width * 2.0 - 1.0, v = 1.0 - (y + (sy + 0.5) / g.spp) / g.height * 2.0;
            const double dx = u * th * aspect, dy = v * th, l = std::sqrt(dx * dx + dy * dy + 1.0);
            const Rgb L = g.env.sample(0, {dx / l, dy / l, -1.0 / l});
            acc = {acc.r + L.r * inv, acc.g + L.g * inv, acc.b + L.b * inv};
        }
        g.background[p] = {acc.r * miss * g.exposure, acc.g * miss * g.exposure, acc.b * miss * g.exposure};
    }
}
int sizeArg(int argc, char** argv, std::string& out, int def) {
    int size = def;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else return -1;
    }
    return size;
}
}  // namespace

int envmapsCommand(int argc, char** argv) {
    std::string out = "envmaps";
    const int size = sizeArg(argc, argv, out, 64);
    if (size < 8 || size > 512) { std::printf("usage: raw_native_cli envmaps [--out DIR] [--size 8..512]\n"); return 2; }
    std::filesystem::create_directories(out);
    std::string json = "[";
    for (const EnvMap& e : allEnvs(size, nullptr)) {
        const std::size_t n = e.cube.levelOffset(1);
        const std::string h = sha256Hex(e.cube.rgb.data(), n * sizeof(double));
        const double ex = autoExposure(e.cube);
        std::vector<Rgb> strip(std::size_t(6) * size * size);
        for (int f = 0; f < 6; ++f) for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            const Rgb t = e.cube.texel(0, f, y, x);
            strip[std::size_t(y) * 6 * size + std::size_t(f) * size + x] = {t.r * ex, t.g * ex, t.b * ex};
        }
        writeImages(out + "/" + e.name, 6 * size, size, strip);
        json += std::string(json.size() > 1 ? ",\n " : "\n ") + "{\"name\": \"" + e.name + "\", \"recipe\": \"" + e.recipe + "\", \"size\": " + std::to_string(size) +
                ", \"generator\": \"raw_native_cli envmaps --size " + std::to_string(size) + "\", \"sha256_level0_f64\": \"" + h + "\", \"family\": \"" + kFamily + "\"}";
    }
    json += "\n]\n";
    std::ofstream(out + "/envmaps.lock.json", std::ios::binary) << json;
    std::printf("%s", json.c_str());
    return 0;
}

int relightCommand(int argc, char** argv) {
    std::string out = "relight";
    const int size = sizeArg(argc, argv, out, 256);
    if (size < 32 || size > 2048) { std::printf("usage: raw_native_cli relight [--out DIR] [--size 32..2048]\n"); return 2; }
    std::filesystem::create_directories(out);
    const pbr::Tables tables;
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    std::string html = "<!doctype html><meta charset=utf-8><title>raw-native relighting</title><style>body{font:14px sans-serif;background:#111;color:#ddd}"
                       "img{width:256px;image-rendering:pixelated;margin:2px}td{vertical-align:top}</style><h1>Relighting: own environments</h1><table>";
    for (int scene = 0; scene < 2; ++scene) {
        D3 (*rot)(D3) = scene == 1 ? &gallery::fieldViewToWorld : nullptr;
        const std::vector<EnvMap> envs = allEnvs(64, rot);
        for (const EnvMap& e : envs) {
            gallery::Gallery g = scene == 1 ? gallery::buildField(size, size * 5 / 8, 2) : gallery::build(size, size, 2);
            g.lights.clear(); g.env = e.cube; g.sh = lighting::shProject(g.env); g.exposure = autoExposure(e.cube);
            rebackground(g);
            Cube pre = g.env; lighting::prefilter(pre, lighting::kPrefilterSamples);
            const std::vector<Rgb> img = gallery::resolve(g, gallery::shadeCpu(g, tables, pre, threads));
            const std::string stem = g.name + "_" + e.name;
            writeImages(out + "/" + stem, g.width, g.height, img);
            html += "<tr><td>" + g.name + "<br>" + e.name + "</td><td><img src='" + stem + ".png'>";
            if (e.name == "clear_noon") {   // the G-buffer once per scene
                const std::size_t np = std::size_t(g.width) * g.height;
                std::vector<double> alb(np), nx(np), ny(np), nz(np), rough(np), depth(np), cnt(np);
                for (std::size_t k = 0; k < g.samples.size(); ++k) {
                    const lighting::Sample& s = g.samples[k]; const std::size_t p = std::size_t(g.pixel[k]);
                    alb[p] += lum({s.m.baseColor.r, s.m.baseColor.g, s.m.baseColor.b}); nx[p] += s.n.x; ny[p] += s.n.y; nz[p] += s.n.z;
                    rough[p] += s.m.roughness; depth[p] += -s.p.z; cnt[p] += 1;
                }
                for (std::size_t p = 0; p < np; ++p) if (cnt[p] > 0) { alb[p] /= cnt[p]; nx[p] /= cnt[p]; ny[p] /= cnt[p]; nz[p] /= cnt[p]; rough[p] /= cnt[p]; depth[p] /= cnt[p]; }
                std::vector<std::uint8_t> nrm(np * 3);
                for (std::size_t p = 0; p < np; ++p) { const double v[3] = {nx[p], ny[p], nz[p]}; for (int c = 0; c < 3; ++c) nrm[p * 3 + std::size_t(c)] = std::uint8_t(std::lround(255.0 * std::clamp(v[c] * 0.5 + 0.5, 0.0, 1.0))); }
                writeGrey(out + "/" + g.name + "_albedo.png", g.width, g.height, alb, 0.0, 1.0);
                writePng(out + "/" + g.name + "_normal.png", g.width, g.height, nrm);
                writeGrey(out + "/" + g.name + "_roughness.png", g.width, g.height, rough, 0.0, 1.0);
                writeGrey(out + "/" + g.name + "_depth.png", g.width, g.height, depth, 0.0, 16.0);
                for (const char* ch : {"albedo", "normal", "roughness", "depth"}) html += "<img src='" + g.name + "_" + ch + ".png' title='" + ch + "'>";
            }
            html += "</td></tr>";
        }
    }
    html += "</table><p>Environments are generated by raw-native (analytic skies and a path-traced probe); no measured HDRI.</p>";
    std::ofstream(out + "/index.html", std::ios::binary) << html;
    std::printf("relight: %s/index.html\n", out.c_str());
    return 0;
}
}  // namespace raw
