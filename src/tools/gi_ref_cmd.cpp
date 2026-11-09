// raw_native_cli gi-reference: scenes and path-traced references for the 3D radiance cascades
// (web/shaders/cascades3d, docs/radiance-cascades-3d.md).
//
//   raw_native_cli gi-reference (--scene-file F | scene flags as for a render) [--export F]
//                               [--pt DIR --width W --height H --spp N --bounces B --seed S] [--cpu]
//
// The scene comes from a scene file (web/shaders/cascades3d/scene3d.mjs writes one) or from the
// render flags (--owned-scene raw-hall-nave, ...), lit as pt-error lights it: sky 0.2, sun irradiance
// pi. --export writes the scene as the path tracer sees it (albedo as bytes / 255) in the scene-file
// format. --pt renders it with the path tracer (GPU unless --cpu) and writes radiance.pfm,
// variance.pfm, triangle.i32 and pt.json into DIR. Exit 0 on success, 2 on bad input.
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_pt_convert.hpp"
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/tools/cli_params.hpp"
#include "raw/tools/model_scene.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <tuple>
#include <vector>
namespace raw {
namespace {
constexpr char kMagic[9] = "RNGISC01";
std::uint32_t byteOf(float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// A scene file into a path-tracer scene (one 1 x 1 texture and material per albedo and emission).
bool readSceneFile(const std::string& path, rt::PtScene& s, rt::PtCamera& cam) {
    std::ifstream f(path, std::ios::binary);
    char m[8]; std::uint32_t n = 0;
    if (!f.read(m, 8) || std::memcmp(m, kMagic, 8) != 0 || !f.read(reinterpret_cast<char*>(&n), 4)) return false;
    std::vector<float> h(16), t(std::size_t(n) * 24);
    if (!f.read(reinterpret_cast<char*>(h.data()), 64) || !f.read(reinterpret_cast<char*>(t.data()), std::streamsize(t.size() * 4))) return false;
    s = {};
    s.name = path;
    std::map<std::tuple<std::uint32_t, float, float, float>, std::uint32_t> mats;
    for (std::uint32_t i = 0; i < n; ++i) {
        const float* r = &t[std::size_t(i) * 24];
        const std::uint32_t c = byteOf(r[18]) | (byteOf(r[19]) << 8) | (byteOf(r[20]) << 16) | (255u << 24);
        const auto key = std::make_tuple(c, r[21], r[22], r[23]);
        auto it = mats.find(key);
        if (it == mats.end()) {
            const std::uint32_t tex = s.tex.add(1, 1, {c});
            s.materials.resize(std::size_t(tex) + 1);
            s.materials[tex] = {1.0f, 1.0f, 0.0f, 0.0f, {r[21], r[22], r[23]}};
            it = mats.emplace(key, tex).first;
        }
        for (int v = 0; v < 3; ++v) {
            s.geo.idx.push_back(std::uint32_t(s.geo.pos.size()));
            s.geo.pos.push_back({r[3 * v], r[3 * v + 1], r[3 * v + 2]});
            s.geo.nrm.push_back({r[9 + 3 * v], r[10 + 3 * v], r[11 + 3 * v]});
            s.geo.uv.push_back({0.5f, 0.5f});
        }
        s.geo.triTexture.push_back(it->second);
    }
    s.sunDir = {h[0], h[1], h[2]}; s.sunIrradiance = {h[3], h[4], h[5]}; s.sky = {h[6], h[7], h[8]};
    cam = {{h[9], h[10], h[11]}, {h[12], h[13], h[14]}, {0, 1, 0}, h[15]};
    rt::finishPtScene(s);
    return true;
}
bool writeSceneFile(const std::string& path, const rt::PtScene& s, const rt::PtCamera& cam) {
    std::ofstream f(path, std::ios::binary);
    const swr::Geometry& g = s.geo;
    const std::uint32_t n = std::uint32_t(g.triangles());
    const Vec3 sd = normalize(s.sunDir);
    const float h[16] = {sd.x, sd.y, sd.z, s.sunIrradiance.x, s.sunIrradiance.y, s.sunIrradiance.z, s.sky.x, s.sky.y, s.sky.z,
                         cam.eye.x, cam.eye.y, cam.eye.z, cam.target.x, cam.target.y, cam.target.z, cam.fovy};
    f.write(kMagic, 8); f.write(reinterpret_cast<const char*>(&n), 4); f.write(reinterpret_cast<const char*>(h), 64);
    std::vector<float> r(24);
    for (std::uint32_t i = 0; i < n; ++i) {
        for (int v = 0; v < 3; ++v) {
            const std::uint32_t k = g.idx[std::size_t(i) * 3 + std::size_t(v)];
            const Vec3 p = g.pos[k], q = g.nrm[k];
            r[3 * v] = p.x; r[3 * v + 1] = p.y; r[3 * v + 2] = p.z;
            r[9 + 3 * v] = q.x; r[10 + 3 * v] = q.y; r[11 + 3 * v] = q.z;
        }
        const std::uint32_t tex = g.triTexture[i];
        const swr::TextureSet::Entry& e = s.tex.entries[tex];
        const std::uint32_t c = s.tex.texels[e.offset];
        const rt::PtMaterial& m = s.materials[tex];
        const float k = m.baseFactor / 255.0f;
        r[18] = float(c & 255u) * k; r[19] = float((c >> 8) & 255u) * k; r[20] = float((c >> 16) & 255u) * k;
        r[21] = m.emission.x; r[22] = m.emission.y; r[23] = m.emission.z;
        f.write(reinterpret_cast<const char*>(r.data()), 96);
    }
    return bool(f);
}
bool writePfm(const std::string& path, int w, int h, int ch, const std::vector<float>& v) {
    std::ofstream f(path, std::ios::binary);
    f << (ch == 1 ? "Pf" : "PF") << "\n" << w << " " << h << "\n-1.0\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&v[std::size_t(y) * std::size_t(w) * std::size_t(ch)]), std::streamsize(std::size_t(w) * std::size_t(ch) * 4));
    return bool(f);
}
}  // namespace

int giReferenceCommand(int argc, char** argv) {
    std::string file, out, ptDir;
    rt::PathTraceDesc d;
    d.width = 512; d.height = 320; d.spp = 256; d.maxBounces = 8; d.seed = 1; d.jitter = false;
    bool cpu = false;
    std::vector<const char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--scene-file" && more) file = argv[++i];
        else if (a == "--export" && more) out = argv[++i];
        else if (a == "--pt" && more) ptDir = argv[++i];
        else if (a == "--spp" && more) d.spp = std::uint32_t(std::atoi(argv[++i]));
        else if (a == "--bounces" && more) d.maxBounces = std::uint32_t(std::atoi(argv[++i]));
        else if (a == "--seed" && more) d.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--cpu") cpu = true;
        else {
            if ((a == "--width" || a == "--height") && more) (a == "--width" ? d.width : d.height) = std::atoi(argv[i + 1]);
            rest.push_back(argv[i]);
        }
    }
    rt::PtScene s;
    rt::PtCamera cam;
    if (!file.empty()) {
        if (!readSceneFile(file, s, cam)) { std::printf("gi-reference: cannot read scene file %s\n", file.c_str()); return 2; }
    } else {
        std::string err;
        const auto p = parseArgs(int(rest.size()), rest.data(), err);
        if (!p) { std::printf("gi-reference: %s\n", err.c_str()); return 2; }
        Scene sc = sceneFromParams(*p, nullptr);
        sc.camera = cameraFromParams(*p);
        s = rt::ptFromScene(sc, 0.2f, true);
        cam = rt::ptCameraOf(sc.camera);
    }
    if (out.empty() && ptDir.empty()) {
        std::printf("usage: raw_native_cli gi-reference (--scene-file F | scene flags) [--export F] [--pt DIR --width W --height H --spp N --bounces B --seed S] [--cpu]\n");
        return 2;
    }
    std::printf("gi-reference: %zu triangles\n", s.geo.triangles());
    if (!out.empty() && !writeSceneFile(out, s, cam)) { std::printf("gi-reference: cannot write %s\n", out.c_str()); return 2; }
    if (ptDir.empty()) return 0;
    if (d.width < 8 || d.height < 8 || d.spp < 1) { std::printf("gi-reference: bad size or spp\n"); return 2; }
    d.camera = d.previous = cam;
    std::error_code ec;
    std::filesystem::create_directories(ptDir, ec);
    std::string why;
    rhi::Device* dev = cpu ? nullptr : rhi::device(why);
    const auto t0 = std::chrono::steady_clock::now();
    const rt::PathTraceOutput o = dev ? gpu_check::pathTraceGpu(*dev, s, d) : rt::pathTraceCpu(s, d, 3);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!o.error.empty()) { std::printf("gi-reference: %s\n", o.error.c_str()); return 2; }
    const bool ok = writePfm(ptDir + "/radiance.pfm", o.width, o.height, 3, o.radiance) && writePfm(ptDir + "/variance.pfm", o.width, o.height, 1, o.variance);
    std::ofstream(ptDir + "/triangle.i32", std::ios::binary).write(reinterpret_cast<const char*>(o.triangle.data()), std::streamsize(o.triangle.size() * 4));
    char j[600];
    std::snprintf(j, sizeof j, "{\n \"schema\": \"raw-native.gi-reference/1\",\n \"width\": %d,\n \"height\": %d,\n \"spp\": %u,\n \"bounces\": %u,\n"
                  " \"seed\": %llu,\n \"backend\": \"%s\",\n \"adapter\": \"%s\",\n \"ms\": %.1f\n}\n",
                  o.width, o.height, d.spp, d.maxBounces, (unsigned long long)d.seed, dev ? dev->backendName() : "cpu",
                  dev ? dev->adapter().description.c_str() : "", ms);
    std::ofstream(ptDir + "/pt.json") << j;
    std::printf("%s", j);
    return ok ? 0 : 2;
}
}  // namespace raw
