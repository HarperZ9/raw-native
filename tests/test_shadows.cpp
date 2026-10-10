// Cascaded shadow maps against ray-cast visibility (evidence/m3-shadows-bounds.json), on the CPU:
// cascade stability under a camera pan and turn, hard shadows, and PCSS soft shadows.
//   test_shadows [--full] [--json] [--models DIR]
// --models adds Suzanne and the helmet role's model (evidence/m3-scene-models.json) to the hard
// and soft checks; without --full the hard and soft checks use every fourth pixel.
#include "raw/renderer/shadows.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/bvh.hpp"
#include "raw/tools/model_scene.hpp"
#include "raw/tools/model_manifest.hpp"
#include "raw/core/parallel.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using namespace raw;
using namespace raw::shadows;

namespace {
// The G-buffer with the perspective-correct view depth, the depth the cascades are chosen by.
GBuffer gbufferOf(const Scene& s) {
    RasterOptions ro; ro.perspectiveDepth = true;
    return rasterize(s, 256, 256, nullptr, nullptr, ro);
}
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }
D3 d3(Vec3 v) { return {v.x, v.y, v.z}; }
std::array<ShadowMap, kCascades> mapsFor(const Scene& s, const CascadeSet& cs) {
    std::array<ShadowMap, kCascades> m;
    parallelRows(kCascades, threads(), [&](int k) { m[std::size_t(k)] = rasterizeCascade(s, cs, k, 8); });
    return m;
}
std::array<const ShadowMap*, kCascades> ptrs(const std::array<ShadowMap, kCascades>& m) { return {&m[0], &m[1], &m[2], &m[3]}; }
Bvh bvhOf(const Scene& s) {
    std::vector<Tri> t;
    for (const Mesh& m : s.meshes) for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
        t.push_back({m.positions[std::size_t(m.indices[i])], m.positions[std::size_t(m.indices[i + 1])], m.positions[std::size_t(m.indices[i + 2])]});
    Bvh b; b.build(t);
    return b;
}
bool visible(const Bvh& b, D3 q, D3 dir) { return !b.occluded({{float(q.x), float(q.y), float(q.z)}, {float(dir.x), float(dir.y), float(dir.z)}}, 1e30f); }

struct Stability { long checks{0}, changes{0}, regChecks{0}, regChanges{0}; };
Stability stability(const Scene& base, bool stable) {
    Stability r;
    const GBuffer g = gbufferOf(base);
    std::vector<std::pair<D3, D3>> pts;
    uint64_t s = 99;
    while (pts.size() < 4096) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        const int x = int(s % 256), y = int((s >> 20) % 256);
        if (g.mask.at(x, y)) pts.push_back({d3(g.position.at(x, y)), d3(g.normal.at(x, y))});
    }
    const D3 L = d3(base.lights[0].dir);
    const CascadeSet first = fitCascades(base.camera, L, 40.0, 1024, true);
    const double step = 0.37 * 2.0 * first.c[0].radius / 1024.0;
    const Vec3 f = normalize(base.camera.center - base.camera.eye), right = normalize(cross(f, base.camera.up));
    std::vector<int> prev;
    struct Reg { int k; double tw, fx, fy; };
    std::vector<Reg> prevReg;
    for (int k = 0; k < 48; ++k) {
        Camera cam = base.camera;
        const float pan = float(step * std::min(k, 31));
        cam.eye = cam.eye + right * pan; cam.center = cam.center + right * pan;
        if (k >= 32) {                              // then turn about the eye
            const double a = (k - 31) * 0.25 * 3.14159265358979 / 180.0;
            const Vec3 d = cam.center - cam.eye;
            cam.center = cam.eye + Vec3{float(d.x * std::cos(a) - d.z * std::sin(a)), d.y, float(d.x * std::sin(a) + d.z * std::cos(a))};
        }
        const CascadeSet cs = fitCascades(cam, L, 40.0, 1024, stable);
        const auto maps = mapsFor(base, cs);
        const Vec3 fw = normalize(cam.center - cam.eye);
        std::vector<int> cur(pts.size());
        std::vector<Reg> reg(pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const D3 p = pts[i].first;
            const double depth = (p.x - cam.eye.x) * fw.x + (p.y - cam.eye.y) * fw.y + (p.z - cam.eye.z) * fw.z;
            cur[i] = int(lookup(cs, ptrs(maps), p, pts[i].second, depth, Filter::Hard));
            // Where the point lands in its cascade's texel grid.
            const int kk = cascadeOf(cs, depth);
            reg[i] = {kk, 0, 0, 0};
            if (kk >= 0) {
                const Cascade& q = cs.c[std::size_t(kk)];
                const D3 l = cs.toLight(p);
                const double tw = 2.0 * q.radius / cs.size;
                const double fx = ((l.x - q.centre.x) / q.radius * 0.5 + 0.5) * cs.size, fy = ((l.y - q.centre.y) / q.radius * 0.5 + 0.5) * cs.size;
                reg[i] = {kk, tw, fx - std::floor(fx), fy - std::floor(fy)};
            }
        }
        if (!prev.empty()) for (std::size_t i = 0; i < cur.size(); ++i) {
            ++r.checks; if (cur[i] != prev[i]) ++r.changes;
            const Reg &a = reg[i], &b = prevReg[i];
            if (a.k < 0 || a.k != b.k) continue;
            const auto wrap = [](double d) { d = std::fabs(d); return std::min(d, 1.0 - d); };
            ++r.regChecks;
            if (std::fabs(a.tw - b.tw) > 1e-12 * a.tw || wrap(a.fx - b.fx) > 1e-3 || wrap(a.fy - b.fy) > 1e-3) ++r.regChanges;
        }
        prev = cur; prevReg = reg;
    }
    return r;
}

struct SceneResult { std::string name; long interior{0}, mismatches{0}, softPixels{0}, shadowed{0}, penumbra{0}, cascade0{0}; double softMean{0}, softP95{0}, penumbraMean{0}; };
SceneResult visibilityChecks(const std::string& name, const Scene& sc, int stride) {
    SceneResult r; r.name = name;
    const GBuffer g = gbufferOf(sc);
    const D3 L = d3(sc.lights[0].dir), toL{-L.x, -L.y, -L.z};
    const CascadeSet cs = fitCascades(sc.camera, L, 40.0, 1024, true);
    const auto maps = mapsFor(sc, cs);
    const Bvh bvh = bvhOf(sc);
    const LookupParams lp;
    std::vector<int> mism(256 * 256, -1);
    std::vector<double> soft(256 * 256, -1.0);
    std::vector<int> kind(256 * 256, 0);   // 1 shadowed by the one-ray reference, 2 in the soft penumbra
    parallelRows(256, threads(), [&](int y) {
        for (int x = 0; x < 256; x += 1) {
            if ((y * 256 + x) % stride || !g.mask.at(x, y)) continue;
            const D3 p = d3(g.position.at(x, y)), n = d3(g.normal.at(x, y));
            const double depth = g.depth.at(x, y);
            const int k = cascadeOf(cs, depth);
            if (k < 0) continue;
            const double tw = 2.0 * cs.c[std::size_t(k)].radius / cs.size;
            const D3 q{p.x + n.x * lp.normalOffset * tw, p.y + n.y * lp.normalOffset * tw, p.z + n.z * lp.normalOffset * tw};
            const bool ref = visible(bvh, q, toL);
            // Interior: the reference agrees two texels away in 8 directions on the receiver.
            D3 t{1, 0, 0};
            if (std::fabs(n.x) > 0.9) t = {0, 1, 0};
            const double tn = t.x * n.x + t.y * n.y + t.z * n.z;
            t = {t.x - n.x * tn, t.y - n.y * tn, t.z - n.z * tn};
            const double tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            t = {t.x / tl, t.y / tl, t.z / tl};
            const D3 b{n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x};
            bool interior = true;
            for (int a = 0; a < 8 && interior; ++a) {
                const double ang = a * 3.14159265358979 / 4.0, ox = std::cos(ang) * 2 * tw, oy = std::sin(ang) * 2 * tw;
                interior = visible(bvh, {q.x + t.x * ox + b.x * oy, q.y + t.y * ox + b.y * oy, q.z + t.z * ox + b.z * oy}, toL) == ref;
            }
            const double hard = lookup(cs, ptrs(maps), p, n, depth, Filter::Hard, lp);
            if (interior) mism[std::size_t(y) * 256 + x] = (hard > 0.5) != ref ? 1 : 0;
            // Soft reference: 256 stratified directions over the sun's disc.
            D3 u{1, 0, 0};
            if (std::fabs(toL.x) > 0.9) u = {0, 1, 0};
            const double ud = u.x * toL.x + u.y * toL.y + u.z * toL.z;
            u = {u.x - toL.x * ud, u.y - toL.y * ud, u.z - toL.z * ud};
            const double ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
            u = {u.x / ul, u.y / ul, u.z / ul};
            const D3 w{toL.y * u.z - toL.z * u.y, toL.z * u.x - toL.x * u.z, toL.x * u.y - toL.y * u.x};
            const double ts = std::tan(lp.sunAngle);
            int lit = 0;
            for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) {
                const double rr = std::sqrt((i + 0.5) / 16.0) * ts, ph = 2 * 3.14159265358979 * (j + 0.5) / 16.0;
                D3 d{toL.x + (u.x * std::cos(ph) + w.x * std::sin(ph)) * rr, toL.y + (u.y * std::cos(ph) + w.y * std::sin(ph)) * rr,
                     toL.z + (u.z * std::cos(ph) + w.z * std::sin(ph)) * rr};
                lit += visible(bvh, q, d);
            }
            kind[std::size_t(y) * 256 + x] = (lit > 0 && lit < 256 ? 2 : 0) | (ref ? 0 : 1) | (k == 0 ? 4 : 0);
            soft[std::size_t(y) * 256 + x] = std::fabs(lookup(cs, ptrs(maps), p, n, depth, Filter::Pcss, lp) - lit / 256.0);
        }
    });
    std::vector<double> sv;
    for (int m : mism) if (m >= 0) { ++r.interior; r.mismatches += m; }
    for (double v : soft) if (v >= 0.0) sv.push_back(v);
    for (int v : kind) { r.shadowed += v & 1; r.penumbra += (v >> 1) & 1; r.cascade0 += (v >> 2) & 1; }
    for (std::size_t i = 0; i < kind.size(); ++i) if (kind[i] & 2) r.penumbraMean += soft[i] / double(r.penumbra);   // reported, not bounded
    if (const char* dir = std::getenv("RAW_NATIVE_SHADOW_DUMP")) {   // diagnosis: the reference's classes as a PGM
        std::string path = std::string(dir) + "/" + name + ".pgm";
        for (char& ch : path) if (ch == ' ' || ch == ',') ch = '_';
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P5 256 256 255\n");
            for (int i = 0; i < 256 * 256; ++i) {
                const unsigned char v = !g.mask.px[std::size_t(i)] ? 0 : (kind[std::size_t(i)] & 2) ? 128 : (kind[std::size_t(i)] & 1) ? 60 : 255;
                std::fputc(v, f);
            }
            std::fclose(f);
        }
    }
    std::sort(sv.begin(), sv.end());
    r.softPixels = long(sv.size());
    for (double v : sv) r.softMean += v / double(sv.size());
    if (!sv.empty()) r.softP95 = sv[std::min(sv.size() - 1, std::size_t(0.95 * sv.size()))];
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    bool json = false, full = false, stats = false;
    std::string models;
    for (int i = 1; i < argc; ++i) {
        json = json || !std::strcmp(argv[i], "--json"); full = full || !std::strcmp(argv[i], "--full");
        if (!std::strcmp(argv[i], "--models") && i + 1 < argc) models = argv[++i];
        stats = stats || !std::strcmp(argv[i], "--scene-stats");
    }
    full = full || json;
    CliParams p; p.width = p.height = 256;
    std::vector<std::pair<std::string, Scene>> scenes{{"built-in test scene", sceneFromParams(p, nullptr)}};
    scenes.push_back({"test scene, near camera", scenes[0].second});   // method note 2: cascade 0 in use
    scenes[1].second.camera.eye = {1.8f, 3.2f, 2.4f}; scenes[1].second.camera.center = {0.8f, 0.2f, 1.0f};   // method note 5
    if (!models.empty()) {
        std::string err;
        p.model = models + "/Models/Suzanne/glTF/Suzanne.gltf";
        scenes.push_back({"Suzanne", sceneFromParams(p, nullptr)});
        p.model = resolveModelRole(std::string(RAW_SOURCE_DIR) + "/evidence/m3-scene-models.json", "helmet", models, err);
        if (!p.model.empty()) scenes.push_back({"helmet role", sceneFromParams(p, nullptr)});
    }
    for (auto& sc : scenes) sc.second.lights[0].dir = normalize(Vec3{0.55f, -0.45f, 0.35f});   // the method note's visible sun
    if (stats) {   // scene design only: G-buffer pixels per cascade, no shadow result
        for (const auto& sc : scenes) {
            const GBuffer g = gbufferOf(sc.second);
            const CascadeSet cs = fitCascades(sc.second.camera, d3(sc.second.lights[0].dir), 40.0, 1024, true);
            long n[kCascades + 1] = {0, 0, 0, 0, 0};
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x)
                if (g.mask.at(x, y)) { const int k = cascadeOf(cs, g.depth.at(x, y)); ++n[k < 0 ? kCascades : k]; }
            std::printf("%s: cascade pixels %ld %ld %ld %ld, beyond %ld\n", sc.first.c_str(), n[0], n[1], n[2], n[3], n[4]);
        }
        return 0;
    }
    const Stability st = stability(scenes[0].second, true), ctl = stability(scenes[0].second, false);
    const double stRate = double(st.changes) / double(st.checks), ctlRate = double(ctl.changes) / double(ctl.checks);
    // The original check is reported; its control's power is recorded, not gated (method note).
    const bool discriminates = stRate <= 0.001 && ctlRate > 0.01;
    const double regRate = double(st.regChanges) / double(st.regChecks), regCtl = double(ctl.regChanges) / double(ctl.regChecks);
    CHECK(stRate <= 0.001);
    CHECK(regRate <= 0.001);
    CHECK(regCtl > 0.01);
    std::string rows;
    for (const auto& s : scenes) {
        const SceneResult r = visibilityChecks(s.first, s.second, full ? 1 : 4);
        CHECK(r.interior > 1000 / (full ? 1 : 4));
        CHECK(r.shadowed >= 2000 / (full ? 1 : 4) && r.penumbra >= 300 / (full ? 1 : 4));   // the method note's coverage bound
        CHECK(double(r.mismatches) <= 0.001 * double(r.interior));
        CHECK(r.softMean <= 0.05 && r.softP95 <= 0.20);
        if (s.first == "test scene, near camera") CHECK(r.cascade0 >= 2000 / (full ? 1 : 4));
        char b[600];
        std::snprintf(b, sizeof b, "%s  {\"scene\": \"%s\", \"interior_pixels\": %ld, \"hard_mismatches\": %ld, \"soft_pixels\": %ld, \"shadowed\": %ld, \"penumbra\": %ld, \"cascade0_pixels\": %ld, \"pcss_mean_abs\": %.4f, \"pcss_p95_abs\": %.4f, \"pcss_penumbra_mean_abs\": %.4f}",
                      rows.empty() ? "" : ",\n", r.name.c_str(), r.interior, r.mismatches, r.softPixels, r.shadowed, r.penumbra, r.cascade0, r.softMean, r.softP95, r.penumbraMean);
        rows += b;
    }
    if (json) std::printf("{\n \"stability\": {\"checks\": %ld, \"changes\": %ld, \"rate\": %.5f, \"control_rate\": %.5f, \"control_discriminates\": %s},\n"
                          " \"texel_registration\": {\"pairs\": %ld, \"changes\": %ld, \"rate\": %.5f, \"control_pairs\": %ld, \"control_rate\": %.5f},\n \"scenes\": [\n%s\n ],\n \"failures\": %d\n}\n",
                          st.checks, st.changes, stRate, ctlRate, discriminates ? "true" : "false", st.regChecks, st.regChanges, regRate, ctl.regChecks, regCtl,
                          rows.c_str(), raw_test_failures());
    else std::printf("stability %.5f (control %.5f)  registration %.5f (control %.5f)\n%s\n", stRate, ctlRate, regRate, regCtl, rows.c_str());
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
