// Source-style baked lighting against converged path tracing (evidence/m3-bake-bounds.json).
//   test_bake [--full] [--json]
// Without --full (as ctest runs it) a quarter of the points of each check run, with the
// same path counts; CI runs --full, and --json implies it.
#include "raw/renderer/bake.hpp"
#include "raw/core/parallel.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
using namespace raw::bake;

namespace {
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
double rel(Rgb a, Rgb b) { return std::fabs(lum(a) - lum(b)) / lum(b); }
struct Stats { int n{0}; double median{0}, p95{0}, worst{0}; };
Stats stats(std::vector<double> e) {
    std::vector<double> v;
    for (double x : e) if (x >= 0.0) v.push_back(x);
    std::sort(v.begin(), v.end());
    Stats s; s.n = int(v.size());
    if (!v.empty()) { s.median = v[v.size() / 2]; s.p95 = v[std::min(v.size() - 1, std::size_t(0.95 * v.size()))]; s.worst = v.back(); }
    return s;
}
std::uint64_t mix(std::uint64_t x) { x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull; x ^= x >> 33; return x; }
double u01(std::uint64_t& s) { s = mix(s + 0x9E3779B97F4A7C15ull); return double(s >> 11) * (1.0 / 9007199254740992.0); }
D3 tilt(D3 n, D3 t, D3 b, double maxAngle, std::uint64_t& s) {   // a normal within maxAngle of n
    const double th = maxAngle * std::sqrt(u01(s)), ph = 2.0 * raw::pbr::kPi * u01(s);
    const double c = std::cos(th), sn = std::sin(th), x = sn * std::cos(ph), y = sn * std::sin(ph);
    return {t.x * x + b.x * y + n.x * c, t.y * x + b.y * y + n.y * c, t.z * x + b.z * y + n.z * c};
}
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }
// A point inside a box: the first surface straight up shows its back.
bool insideSolid(const Tracer& tr, D3 p) {
    double t;
    const int q = tr.trace(p, {0, 1, 0}, t);
    if (q < 0) return true;
    D3 qt, qb, n; quadFrame(tr.room().quads[std::size_t(q)], qt, qb, n);
    return n.y > 0.0;
}
}  // namespace

int main(int argc, char** argv) {
    bool json = false, full = false;
    for (int i = 1; i < argc; ++i) { json = json || !std::strcmp(argv[i], "--json"); full = full || !std::strcmp(argv[i], "--full"); }
    full = full || json;
    const int ref = 16384, bake = 1024, cubePaths = 4096, part = full ? 1 : 4;
    // 1. The path tracer's furnace.
    const Tracer box(furnaceBox(0.5, 1.0));
    std::vector<double> furn(std::size_t(64 / part));
    raw::parallelRows(64 / part, threads(), [&](int k) {
        std::uint64_t s = 1000 + std::uint64_t(k);
        const D3 p{1.6 * u01(s) - 0.8, 1.6 * u01(s) - 0.8, 1.6 * u01(s) - 0.8};
        const double z = 2 * u01(s) - 1, ph = 2 * raw::pbr::kPi * u01(s), r = std::sqrt(1 - z * z);
        furn[std::size_t(k)] = std::fabs(box.irradiance(p, {r * std::cos(ph), r * std::sin(ph), z}, ref, 77 + std::uint64_t(k)).g / 2.0 - 1.0);
    });
    const double furnWorst = *std::max_element(furn.begin(), furn.end());
    // 2 and 3. Radiosity normal maps on 256 seeded texels.
    const Room room = testRoom();
    const Tracer tr(room);
    std::vector<Lightmap> maps = bakeLightmaps(tr, 8.0, 1, 1);     // the texel layout (values replaced below)
    double mean = 0.0; int count = 0;
    for (const Lightmap& lm : maps) for (int k = 0; k < lm.n1 * lm.n2; k += 7) { mean += lum(tr.irradiance(lm.texelCentre(room, k % lm.n1, k / lm.n1), [&] { D3 t, b, n; quadFrame(room.quads[std::size_t(lm.quad)], t, b, n); return n; }(), 64, 5)); ++count; }
    mean /= count;
    std::vector<double> eFlat(std::size_t(256 / part), -1.0), eBump(std::size_t(256 / part), -1.0);
    raw::parallelRows(256 / part, threads(), [&](int k) {
        std::uint64_t s = 5000 + std::uint64_t(k);
        Lightmap lm = maps[std::size_t(u01(s) * maps.size())];
        const int i = int(u01(s) * lm.n1), j = int(u01(s) * lm.n2);
        D3 t, b, n; quadFrame(room.quads[std::size_t(lm.quad)], t, b, n);
        const D3 p = lm.texelCentre(room, i, j);
        bakeTexel(tr, lm, i, j, bake, s);
        const Rgb truthFlat = tr.irradiance(p, n, ref, s + 101);
        if (lum(truthFlat) > 0.01 * mean) eFlat[std::size_t(k)] = rel(shadeRnm(lm, i, j, {0, 0, 1}), truthFlat);
        const D3 nb = tilt({0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 30.0 * raw::pbr::kPi / 180.0, s);
        const D3 nw{t.x * nb.x + b.x * nb.y + n.x * nb.z, t.y * nb.x + b.y * nb.y + n.y * nb.z, t.z * nb.x + b.z * nb.y + n.z * nb.z};
        const Rgb truthBump = tr.irradiance(p, nw, ref, s + 202);
        if (lum(truthBump) > 0.01 * mean) eBump[std::size_t(k)] = rel(shadeRnm(lm, i, j, nb), truthBump);
    });
    // 4. Ambient cubes at 64 points, 16 normals each.
    std::vector<double> eCube(std::size_t(64 / part) * 16, -1.0);
    raw::parallelRows(64 / part, threads(), [&](int k) {
        std::uint64_t s = 9000 + std::uint64_t(k);
        D3 p;
        do { p = {3.6 * u01(s) - 1.8, 0.1 + 2.8 * u01(s), 3.6 * u01(s) - 1.8}; } while (insideSolid(tr, p));
        const AmbientCube cube = bakeAmbientCube(tr, p, cubePaths, s);
        for (int q = 0; q < 16; ++q) {
            const double z = 2 * u01(s) - 1, ph = 2 * raw::pbr::kPi * u01(s), r = std::sqrt(1 - z * z);
            const D3 n{r * std::cos(ph), r * std::sin(ph), z};
            const Rgb truth = tr.irradiance(p, n, ref, s * 16 + std::uint64_t(q));
            const Rgb shade = shadeAmbientCube(cube, n), d = tr.directLight(p, n);    // the point light shaded directly, as Source does
            if (lum(truth) > 0.01 * mean) eCube[std::size_t(k) * 16 + std::size_t(q)] = rel({shade.r + d.r, shade.g + d.g, shade.b + d.b}, truth);
        }
    });
    // 5. Determinism: one thread against every thread.
    const auto a = bakeLightmaps(tr, 4.0, 8, 1), b = bakeLightmaps(tr, 4.0, 8, threads());
    bool same = a.size() == b.size();
    for (std::size_t k = 0; same && k < a.size(); ++k) for (int c = 0; c < 3; ++c) same = same && !std::memcmp(a[k].basis[c].data(), b[k].basis[c].data(), a[k].basis[c].size() * sizeof(Rgb));
    const Stats sf = stats(eFlat), sb = stats(eBump), sc = stats(eCube);
    CHECK(furnWorst <= 0.01);
    CHECK(sf.n > 128 / part && sf.median <= 0.10 && sf.p95 <= 0.25);
    CHECK(sb.n > 128 / part && sb.median <= 0.15 && sb.p95 <= 0.35);
    CHECK(sc.n > 512 / part && sc.median <= 0.15 && sc.p95 <= 0.40);
    CHECK(same);
    const char* fmt = json ? "{\n \"pt_furnace_worst\": %.4f,\n \"rnm_face_normal\": {\"n\": %d, \"median\": %.4f, \"p95\": %.4f, \"worst\": %.4f},\n"
                             " \"rnm_bumped\": {\"n\": %d, \"median\": %.4f, \"p95\": %.4f, \"worst\": %.4f},\n"
                             " \"ambient_cube\": {\"n\": %d, \"median\": %.4f, \"p95\": %.4f, \"worst\": %.4f},\n \"deterministic\": %s,\n"
                             " \"paths\": {\"reference\": %d, \"bake\": %d, \"cube\": %d}\n}\n"
                           : "furnace %.4f; RNM flat n %d median %.4f p95 %.4f worst %.4f; bumped n %d median %.4f p95 %.4f worst %.4f; "
                             "cube n %d median %.4f p95 %.4f worst %.4f; deterministic %s; paths %d/%d/%d\n";
    std::printf(fmt, furnWorst, sf.n, sf.median, sf.p95, sf.worst, sb.n, sb.median, sb.p95, sb.worst, sc.n, sc.median, sc.p95, sc.worst,
                same ? "true" : "false", ref, bake, cubePaths);
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
