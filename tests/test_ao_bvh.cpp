// The BVH-backed AO reference against the scan of every triangle (check g1 of
// evidence/m3-ao-bvh-bounds.json), on small scenes the scan finishes on: the built-in test
// scene, a grid of spheres on a floor, and raw-hero at 64 x 40. Bound: bit-identical on at
// least 99.99% of covered pixels, no pixel more than one sample apart. The skew control
// (RAW_NATIVE_AO_ORACLE=skew, g4) must break it. Times are printed, not judged (g5).
#include "raw/renderer/ray_ao.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/tools/owned_assets.hpp"
#include "check.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace raw;
namespace {
void setOracle(const char* v){
#if defined(_WIN32)
    _putenv_s("RAW_NATIVE_AO_ORACLE", v);
#else
    setenv("RAW_NATIVE_AO_ORACLE", v, 1);
#endif
}
// A UV sphere of radius r at c, 16 x 12 quads.
void addSphere(Mesh& m, Vec3 c, float r){
    const int nu = 16, nv = 12;
    const int base = (int)m.positions.size();
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i){
            const float th = 3.14159265f * float(j) / nv, ph = 6.2831853f * float(i) / nu;
            const Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
            m.positions.push_back(c + n * r); m.normals.push_back(n);
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i){
            const int a = base + j * (nu + 1) + i, b = a + 1, d = a + nu + 1, e = d + 1;
            for (int k : {a, d, b, b, d, e}) m.indices.push_back(k);
        }
}
Scene spheres(int w, int h){
    Scene s = buildTestScene(w, h);
    s.meshes.resize(1);   // keep the floor, drop the box
    Mesh m;
    for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) addSphere(m, {-2.4f + 2.4f * i, 0.8f, -2.4f + 2.4f * k}, 0.8f);
    s.meshes.push_back(std::move(m));
    return s;
}
struct Result { int covered{0}, identical{0}; float worst{0}; double msBvh{0}, msScan{0}; };
Result compare(const Scene& s, int w, int h, int samples, const char* mode){
    const GBuffer g = rasterize(s, w, h);
    Result r;
    setOracle(mode);
    LinearAccel fast; fast.build(s);
    auto t0 = std::chrono::steady_clock::now();
    const Buffer<float> a = computeRTAO(g, fast, samples, 2.0f);
    r.msBvh = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    setOracle("scan");
    LinearAccel scan; scan.build(s);
    t0 = std::chrono::steady_clock::now();
    const Buffer<float> b = computeRTAO(g, scan, samples, 2.0f);
    r.msScan = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    setOracle("");
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x){
        if (!g.mask.at(x, y)) continue;
        ++r.covered;
        const float d = std::fabs(a.at(x, y) - b.at(x, y));
        r.identical += d == 0.0f;
        r.worst = std::fmax(r.worst, d);
    }
    return r;
}
bool holds(const Result& r, int samples){
    return r.covered > 0 && r.identical >= 0.9999 * r.covered && r.worst <= 1.0f / samples + 1e-7f;
}
}  // namespace

int main(){
    const Scene test = buildTestScene(128, 128);
    const Scene grid = spheres(96, 96);
    const Scene hero = owned::inTestScene(owned::hero(), 64, 40);
    struct Case { const char* name; const Scene* s; int w, h, samples; } cases[] = {
        {"test scene 128x128", &test, 128, 128, 64}, {"sphere grid 96x96", &grid, 96, 96, 64}, {"raw-hero 64x40", &hero, 64, 40, 16}};
    int skewBroke = 0;
    for (const Case& c : cases){
        const Result r = compare(*c.s, c.w, c.h, c.samples, "");
        std::printf("%s: %d of %d covered pixels identical, worst %.6f; bvh %.1f ms, scan %.1f ms\n",
                    c.name, r.identical, r.covered, r.worst, r.msBvh, r.msScan);
        CHECK(holds(r, c.samples));
        const Result k = compare(*c.s, c.w, c.h, c.samples, "skew");
        std::printf("  skew control: %d of %d identical, worst %.6f\n", k.identical, k.covered, k.worst);
        skewBroke += !holds(k, c.samples);
    }
    CHECK(skewBroke > 0);   // the control must break the guard on at least one scene
    return raw_test_summary();
}
