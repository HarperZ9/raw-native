// Lighting checks on the CPU (evidence/m3-lighting-bounds.json): physical units, the IBL
// furnace, the split sum against importance-sampled integration, and the clusters.
//   test_lighting [--full] [--json]
// Without --full (as ctest runs it) the split-sum comparison uses the first 512 of its 2,048
// samples; CI runs --full, and --json implies it.
#include "raw/renderer/lighting.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/renderer/lighting_scenes.hpp"
#include "raw/core/parallel.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
using namespace raw::lighting;
using namespace raw::lighting::scenes;

namespace {
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }
double rel(double a, double b) { return std::fabs(a - b) / std::max(std::fabs(b), 1e-300); }
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

double unitsWorst() {
    double w = 0.0;
    D3 L;
    Light p; p.type = LightType::Point; p.position = {0, 0, -3}; p.intensity = 800;
    Rgb e = illuminance(p, {0, 0, -5}, L);
    w = std::max(w, rel(e.r, 800.0 / 4.0));
    Light d; d.type = LightType::Directional; d.direction = {0, -1, 0}; d.intensity = 1e5;
    e = illuminance(d, {1, 2, -3}, L);
    w = std::max({w, rel(e.g, 1e5), rel(L.y, 1.0)});
    Light s; s.type = LightType::Spot; s.position = {0, 2, -4}; s.direction = {0, -1, 0}; s.intensity = 300;
    s.innerCone = 0.3; s.outerCone = 0.6; s.range = 10;
    const D3 q{std::tan(0.45) * 2.0, 0.0, -4.0};                // 0.45 rad off the axis, 2 below
    e = illuminance(s, q, L);
    const double dist = std::sqrt(q.x * q.x + 4.0), cd = 2.0 / dist;
    const double sc = 1.0 / (std::cos(0.3) - std::cos(0.6)), a = std::clamp(cd * sc - std::cos(0.6) * sc, 0.0, 1.0);
    const double win = std::pow(std::clamp(1.0 - std::pow(dist / 10.0, 4.0), 0.0, 1.0), 2.0);
    w = std::max(w, rel(e.r, 300.0 / (dist * dist) * win * a * a));
    w = std::max(w, rel(exposure(ev100(16.0, 1.0 / 125.0, 100.0)), 1.0 / (1.2 * 16.0 * 16.0 * 125.0)));
    return w;
}

Rgb uniformSky(D3) { return {1000.0, 1000.0, 1000.0}; }

double iblFurnaceWorst(const raw::pbr::Tables& t) {
    Cube c = cubeFrom(uniformSky, 32, 6);
    prefilter(c, kPrefilterSamples);
    const std::vector<Rgb> sh = shProject(c);
    double w = 0.0;
    for (int kr = 1; kr <= 20; ++kr) for (int km = 1; km <= 20; ++km) {
        Sample s; s.m.roughness = kr * 0.05;
        const double mu = km * 0.05;
        s.v = {std::sqrt(1 - mu * mu), 0, mu};
        const Rgb o = shadeIbl(s, t, c, sh);
        w = std::max({w, std::fabs(o.r / 1000.0 - 1.0), std::fabs(o.g / 1000.0 - 1.0), std::fabs(o.b / 1000.0 - 1.0)});
    }
    return w;
}

struct SplitSum { int used{0}; double median{0}, p95{0}, refNoise{0}; };
SplitSum splitSumError(const raw::pbr::Tables& t, int count) {
    Cube c = cubeFrom(proceduralSky, 32, 6);
    prefilter(c, kPrefilterSamples);
    const std::vector<Rgb> sh = shProject(c);
    double mean = 0.0;
    for (std::size_t i = 0; i < std::size_t(6 * 32 * 32); ++i) mean += lum(c.texel(0, int(i / 1024), int(i / 32 % 32), int(i % 32)));
    mean /= 6 * 32 * 32;
    std::vector<double> err(std::size_t(count), -1.0), noise(std::size_t(count), -1.0);
    raw::parallelRows(count, threads(), [&](int k) {
        Rng r{0xC0FFEEull + std::uint64_t(k) * 7919u};
        Sample s;
        s.n = unit(randomDir(r)); s.v = unit(randomDir(r));
        if (s.n.x * s.v.x + s.n.y * s.v.y + s.n.z * s.v.z < 0.05) s.v = {-s.v.x, -s.v.y, -s.v.z};
        if (s.n.x * s.v.x + s.n.y * s.v.y + s.n.z * s.v.z < 0.05) return;
        D3 tb, bb; cube::basis(s.n, tb, bb); s.t = tb;
        s.m.baseColor = {r.next(), r.next(), r.next()}; s.m.roughness = 0.1 + 0.9 * r.next(); s.m.metallic = r.next() < 0.5 ? 1.0 : 0.0;
        const Rgb ref = shadeIblReference(s, t, c, 4096), fast = shadeIbl(s, t, c, sh);
        if (lum(ref) > 0.01 * mean) err[std::size_t(k)] = rel(lum(fast), lum(ref));
        // The reference's own noise, on every 16th sample: 4,096 against 16,384 samples a strategy.
        if (k % 16 == 0 && err[std::size_t(k)] >= 0.0) noise[std::size_t(k)] = rel(lum(ref), lum(shadeIblReference(s, t, c, 16384)));
        if (std::getenv("RAW_NATIVE_LIGHTING_DUMP") && err[std::size_t(k)] > 0.2)   // diagnosis
            std::fprintf(stderr, "split %d metal %.0f rough %.3f nv %.3f n.y %.3f ref %.1f fast %.1f err %.3f\n", k, s.m.metallic, s.m.roughness,
                         s.n.x * s.v.x + s.n.y * s.v.y + s.n.z * s.v.z, s.n.y, lum(ref), lum(fast), err[std::size_t(k)]);
    });
    std::vector<double> e;
    for (double x : err) if (x >= 0.0) e.push_back(x);
    std::sort(e.begin(), e.end());
    SplitSum o; o.used = int(e.size());
    for (double x : noise) o.refNoise = std::max(o.refNoise, x);
    if (!e.empty()) { o.median = e[e.size() / 2]; o.p95 = e[std::min(e.size() - 1, std::size_t(0.95 * e.size()))]; }
    return o;
}

struct ClusterResult { long points{0}, misses{0}, overflow{0}; double shadingWorst{0}; };
ClusterResult clusterChecks(const raw::pbr::Tables& t) {
    ClusterResult res;
    const ClusterGrid g;
    for (int sc = 0; sc < 8; ++sc) {
        Rng r{1000ull + std::uint64_t(sc)};
        const std::vector<Light> ls = scene(r, g, 256);
        int over = 0;
        const auto lists = assignClusters(g, ls, &over);
        res.overflow += over;
        std::vector<long> miss(8192, 0), inside(8192, 0); std::vector<double> worst(8192, 0.0);
        raw::parallelRows(8192, threads(), [&](int k) {
            Rng q{std::uint64_t(sc) * 100003ull + std::uint64_t(k) + 17};
            D3 p = inFrustum(q, g, 0.2, 80.0);
            if (k % 2) {                                  // half the points near a light
                const Light& l = ls[2 + std::size_t(q.next() * 254)];
                const D3 o = randomDir(q); const double rr = l.range * q.next();
                p = {l.position.x + o.x * rr, l.position.y + o.y * rr, l.position.z + o.z * rr};
            }
            const int c = g.clusterOf(p);
            if (c < 0) return;
            inside[std::size_t(k)] = 1;
            const std::vector<int>& list = lists[std::size_t(c)];
            std::vector<int> all;
            for (std::size_t i = 0; i < ls.size(); ++i) {
                D3 L; const Rgb e = illuminance(ls[i], p, L);
                if (e.r == 0.0 && e.g == 0.0 && e.b == 0.0) continue;
                all.push_back(int(i));
                if (std::find(list.begin(), list.end(), int(i)) == list.end()) ++miss[std::size_t(k)];
            }
            const Sample s = sampleAt(q, p, k);
            const Rgb a = shadePunctual(s, t, ls, list), b = shadePunctual(s, t, ls, all);
            worst[std::size_t(k)] = std::max({rel(a.r, b.r) * (b.r != 0), rel(a.g, b.g) * (b.g != 0), rel(a.b, b.b) * (b.b != 0)});
        });
        for (int k = 0; k < 8192; ++k) { res.misses += miss[std::size_t(k)]; res.shadingWorst = std::max(res.shadingWorst, worst[std::size_t(k)]); }
        for (long x : inside) res.points += x;           // points drawn outside the frustum are not counted
    }
    return res;
}
}  // namespace

int main(int argc, char** argv) {
    bool json = false, full = false;
    for (int i = 1; i < argc; ++i) { json = json || !std::strcmp(argv[i], "--json"); full = full || !std::strcmp(argv[i], "--full"); }
    full = full || json;
    const raw::pbr::Tables t;
    const double units = unitsWorst(), furnace = iblFurnaceWorst(t);
    const SplitSum ss = splitSumError(t, full ? 2048 : 512);
    const ClusterResult cl = clusterChecks(t);
    CHECK(units <= 1e-12);
    CHECK(furnace <= 1e-3);
    CHECK(ss.used > (full ? 1000 : 250) && ss.median <= 0.05 && ss.p95 <= 0.20);
    CHECK(cl.misses == 0);
    CHECK(cl.shadingWorst <= 1e-6);
    if (json)
        std::printf("{\n \"units_worst_rel\": %.3e,\n \"ibl_furnace_worst\": %.3e,\n \"split_sum\": {\"samples_used\": %d, \"median_rel\": %.4f, \"p95_rel\": %.4f, \"reference_noise_max_rel\": %.4f},\n"
                    " \"clusters\": {\"points\": %ld, \"misses\": %ld, \"overflow_cut\": %ld, \"shading_worst_rel\": %.3e},\n \"failures\": %d\n}\n",
                    units, furnace, ss.used, ss.median, ss.p95, ss.refNoise, cl.points, cl.misses, cl.overflow, cl.shadingWorst, raw_test_failures());
    else
        std::printf("units %.3e; IBL furnace %.3e; split sum median %.4f p95 %.4f over %d (reference noise up to %.4f); clusters %ld points, %ld misses, %ld cut, shading %.3e\n",
                    units, furnace, ss.median, ss.p95, ss.used, ss.refNoise, cl.points, cl.misses, cl.overflow, cl.shadingWorst);
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
