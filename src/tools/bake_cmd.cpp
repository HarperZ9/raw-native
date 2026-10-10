// raw_native_cli bake-room: see raw/tools/bake_cmd.hpp.
#include "raw/tools/bake_cmd.hpp"
#include "raw/tools/image_out.hpp"
#include "raw/renderer/bake.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
namespace raw {
namespace {
using bake::D3;
using bake::Rgb;
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
constexpr double kExposure = 0.12, kTile = 0.5, kBump = 0.02;
// The tangent-space normal of an egg-crate relief h = kBump (cos(2 pi s / kTile) + cos(2 pi t / kTile))
// over a wall's metric coordinates s, t; boxes stay flat.
D3 bumpNormal(double s, double t, bool bumped) {
    if (!bumped) return {0, 0, 1};
    const double w = 2.0 * pbr::kPi / kTile;
    return unit({kBump * w * std::sin(w * s), kBump * w * std::sin(w * t), 1.0});
}
struct Pixel { int quad{-1}; double u{0}, v{0}; D3 p, nt; };
}  // namespace

int bakeCommand(int argc, char** argv) {
    std::string out = "bake-room";
    int size = 384, spp = 256, paths = 256;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--spp") && i + 1 < argc) spp = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--paths") && i + 1 < argc) paths = std::atoi(argv[++i]);
        else { std::printf("usage: raw_native_cli bake-room [--out DIR] [--size N] [--spp K] [--paths P]\n"); return 2; }
    }
    if (size < 16 || size > 2048 || spp < 1 || paths < 1) { std::printf("bake-room: --size 16..2048, --spp and --paths at least 1\n"); return 2; }
    std::filesystem::create_directories(out);
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    const bake::Room room = bake::testRoom();
    const bake::Tracer tr(room);
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<bake::Lightmap> maps = bake::bakeLightmaps(tr, 8.0, paths, threads);
    const double bakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::vector<int> mapOf(room.quads.size(), -1);
    for (std::size_t k = 0; k < maps.size(); ++k) mapOf[std::size_t(maps[k].quad)] = int(k);
    // Primary hits from a camera in the room looking at the back wall.
    const D3 eye{0.0, 1.45, 1.92};
    const double th = std::tan(0.72);
    std::vector<Pixel> px(std::size_t(size) * size);
    std::vector<Rgb> lit(px.size()), ref(px.size());
    parallelRows(size, threads, [&](int y) {
        for (int x = 0; x < size; ++x) {
            const D3 d = unit({((x + 0.5) / size * 2 - 1) * th, (1 - (y + 0.5) / size * 2) * th, -1.0});
            Pixel& p = px[std::size_t(y) * size + x];
            double t;
            p.quad = tr.trace(eye, d, t);
            if (p.quad < 0) continue;
            const bake::Quad& q = room.quads[std::size_t(p.quad)];
            p.p = {eye.x + d.x * t, eye.y + d.y * t, eye.z + d.z * t};
            const D3 rel{p.p.x - q.o.x, p.p.y - q.o.y, p.p.z - q.o.z};
            p.u = dot(rel, q.e1) / dot(q.e1, q.e1); p.v = dot(rel, q.e2) / dot(q.e2, q.e2);
            p.nt = bumpNormal(p.u * std::sqrt(dot(q.e1, q.e1)), p.v * std::sqrt(dot(q.e2, q.e2)), p.quad < 6);
            D3 tt, bb, n; bake::quadFrame(q, tt, bb, n);
            const D3 nw = unit({tt.x * p.nt.x + bb.x * p.nt.y + n.x * p.nt.z, tt.y * p.nt.x + bb.y * p.nt.y + n.y * p.nt.z, tt.z * p.nt.x + bb.z * p.nt.y + n.z * p.nt.z});
            const Rgb e = mapOf[std::size_t(p.quad)] >= 0 ? bake::shadeRnmAt(maps[std::size_t(mapOf[std::size_t(p.quad)])], p.u, p.v, p.nt) : Rgb{};
            const Rgb r = q.albedo.max3() > 0 ? tr.irradiance(p.p, nw, spp, std::uint64_t(y) * 65536 + std::uint64_t(x)) : Rgb{};
            lit[std::size_t(y) * size + x] = {(q.emission.r + q.albedo.r * e.r) * kExposure, (q.emission.g + q.albedo.g * e.g) * kExposure, (q.emission.b + q.albedo.b * e.b) * kExposure};
            ref[std::size_t(y) * size + x] = {(q.emission.r + q.albedo.r * r.r) * kExposure, (q.emission.g + q.albedo.g * r.g) * kExposure, (q.emission.b + q.albedo.b * r.b) * kExposure};
        }
    });
    if (!writeImages(out + "/room-rnm", size, size, lit) || !writeImages(out + "/room-reference", size, size, ref)) {
        std::printf("bake-room: cannot write %s\n", out.c_str()); return 2;
    }
    double se = 0.0, sum = 0.0;
    for (std::size_t k = 0; k < px.size(); ++k) {
        const double a = 0.2126 * lit[k].r + 0.7152 * lit[k].g + 0.0722 * lit[k].b, b = 0.2126 * ref[k].r + 0.7152 * ref[k].g + 0.0722 * ref[k].b;
        se += (a - b) * (a - b); sum += b;
    }
    std::ofstream j(out + "/room.json", std::ios::binary);
    j << "{\n \"what\": \"raw-native Source-style baked lighting (M3): radiosity normal maps against a path trace\",\n \"size\": " << size
      << ",\n \"reference_paths_per_pixel\": " << spp << ",\n \"bake_paths_per_texel_and_basis\": " << paths << ",\n \"texels_per_metre\": 8,\n \"bake_ms\": " << bakeMs
      << ",\n \"exposure\": " << kExposure << ",\n \"display\": \"agx/srgb\",\n \"luminance_rmse_relative_to_mean\": " << std::sqrt(se / double(px.size())) / (sum / double(px.size())) << "\n}\n";
    std::printf("bake-room: %s (%dx%d, bake %.0f ms)\n", out.c_str(), size, size, bakeMs);
    return 0;
}
}  // namespace raw
