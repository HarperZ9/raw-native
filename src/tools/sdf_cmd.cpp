// raw_native_cli sdf-render: see raw/tools/sdf_cmd.hpp.
#include "raw/tools/sdf_cmd.hpp"
#include "raw/renderer/sdf_gpu.hpp"
#include "raw/renderer/sdf_scenes.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
bool writePfm(const std::string& path, int w, int h, int ch, const std::vector<float>& v) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << (ch == 1 ? "Pf" : "PF") << "\n" << w << " " << h << "\n-1.0\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&v[std::size_t(y) * std::size_t(w) * std::size_t(ch)]), std::streamsize(std::size_t(w) * std::size_t(ch) * 4));
    return bool(f);
}
// Shade one frame: albedo (0.2 sky ambient x AO + sun x soft shadow x lambert), then fog and
// god rays. The sky colour and sun colour are fixed showcase choices, not part of any check.
void render(rhi::Device& dev, const sdf::Scene& s, int w, int h, const std::string& out) {
    gpu_check::SdfJob j;
    j.w = w; j.h = h; j.god = s.fogA > 0.0f;
    gpu_check::SdfGpu g = gpu_check::sdfGpu(dev, s, j);
    if (!g.error.empty()) { std::printf("sdf-render: %s\n", g.error.c_str()); return; }
    const std::size_t n = std::size_t(w) * std::size_t(h);
    const Vec3 f = normalize(s.target - s.eye), sd = normalize(cross(f, s.up)), u = cross(sd, f);
    const float th = std::tan(s.fovy * 0.5f);
    std::vector<float> pts, albedo(n * 3, 0.0f), normal(n * 3, 0.0f), depth(n, 0.0f), mat(n, -1.0f);
    std::vector<std::size_t> at;
    for (std::size_t p = 0; p < n; ++p) {
        if (g.march[p * 8] < 0.0f) continue;
        const float nx = (float(p % std::size_t(w)) + 0.5f) / float(w) * 2.0f - 1.0f, ny = 1.0f - (float(p / std::size_t(w)) + 0.5f) / float(h) * 2.0f;
        const Vec3 d = normalize(f + sd * (nx * th * float(w) / float(h)) + u * (ny * th)), x = s.eye + d * g.march[p * 8];
        const Vec3 nn{g.march[p * 8 + 4], g.march[p * 8 + 5], g.march[p * 8 + 6]};
        pts.insert(pts.end(), {x.x, x.y, x.z, nn.x, nn.y, nn.z});
        at.push_back(p);
        const int m = int(g.march[p * 8 + 1]);
        const Vec3 a = s.materials[std::size_t(std::clamp(m, 0, int(s.materials.size()) - 1))].albedo;
        albedo[p * 3] = a.x; albedo[p * 3 + 1] = a.y; albedo[p * 3 + 2] = a.z;
        normal[p * 3] = nn.x; normal[p * 3 + 1] = nn.y; normal[p * 3 + 2] = nn.z;
        depth[p] = g.march[p * 8] * dot(d, f); mat[p] = float(m);
    }
    j.god = false; j.points = pts;
    const gpu_check::SdfGpu t = gpu_check::sdfGpu(dev, s, j);
    std::vector<std::uint8_t> rgb(n * 3);
    const Vec3 sky{0.62f, 0.7f, 0.82f}, sun{1.0f, 0.93f, 0.8f}, l = normalize(s.sunDir);
    std::vector<Vec3> col(n, sky);
    for (std::size_t i = 0; i < at.size() && t.error.empty(); ++i) {
        const std::size_t p = at[i];
        const Vec3 a{albedo[p * 3], albedo[p * 3 + 1], albedo[p * 3 + 2]}, nn{normal[p * 3], normal[p * 3 + 1], normal[p * 3 + 2]};
        const float lit = std::max(0.0f, dot(nn, l)) * t.terms[i * 2], amb = 0.2f * t.terms[i * 2 + 1];
        col[p] = {a.x * (amb * sky.x + lit * sun.x), a.y * (amb * sky.y + lit * sun.y), a.z * (amb * sky.z + lit * sun.z)};
    }
    float godMax = 1e-6f;
    for (std::size_t p = 0; !g.god.empty() && p < n; ++p) godMax = std::max(godMax, g.god[p * 4]);
    for (std::size_t p = 0; p < n; ++p) {
        Vec3 c = col[p];
        if (!g.god.empty()) c = c * std::exp(-s.fogA * g.god[p * 4 + 2] * 0.3f) + sun * (0.5f * g.god[p * 4] / godMax);   // haze fade, shafts scaled to the frame
        const float k[3] = {c.x, c.y, c.z};
        for (int q = 0; q < 3; ++q) rgb[p * 3 + std::size_t(q)] = std::uint8_t(std::lround(255.0f * std::pow(std::clamp(k[q] / (1.0f + k[q]) * 1.6f, 0.0f, 1.0f), 1.0f / 2.2f)));
    }
    const std::string stem = out + "/" + s.name;
    writePng(stem + ".png", w, h, rgb);
    writePfm(stem + "-albedo.pfm", w, h, 3, albedo); writePfm(stem + "-normal.pfm", w, h, 3, normal);
    writePfm(stem + "-depth.pfm", w, h, 1, depth); writePfm(stem + "-material.pfm", w, h, 1, mat);
    std::printf("sdf-render: %s.png and AOVs\n", stem.c_str());
}
}  // namespace

int sdfRenderCommand(int argc, char** argv) {
    std::string out = "sdf";
    int w = 640, h = 480;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) h = std::atoi(argv[++i]);
        else { std::printf("usage: raw_native_cli sdf-render [--out DIR] [--width W] [--height H]\n"); return 2; }
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) { std::printf("sdf-render: needs a GPU backend and an adapter: %s\n", why.c_str()); return 4; }
    for (const sdf::Scene& s : sdf::ownedScenes()) render(*dev, s, w, h, out);
    return 0;
}

}  // namespace raw
