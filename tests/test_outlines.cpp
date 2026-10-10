// Outlines and toon ramps against analytic references (ROADMAP M2 criterion 11; bounds in
// evidence/m2-outlines-bounds.json). A sphere on the optical axis projects to a circle of
// known radius; a cube's visible creases project to known segments. Every check also runs
// against a deliberately wrong reference, which must fail.
//   test_outlines [--json]
#include "raw/renderer/outline.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/math/mat.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <cstring>
#include <functional>
#include <vector>
using namespace raw;

namespace {
constexpr int W = 512, H = 512;
constexpr float FOVY = 0.8f;

Mesh sphere(Vec3 c, float r, int seg = 96, int rings = 48) {
    Mesh m;
    for (int j = 0; j <= rings; ++j) for (int i = 0; i <= seg; ++i) {
        const float th = 3.14159265f * j / rings, ph = 2 * 3.14159265f * i / seg;
        const Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        m.positions.push_back(c + n * r); m.normals.push_back(n);
    }
    for (int j = 0; j < rings; ++j) for (int i = 0; i < seg; ++i) {
        const int a = j * (seg + 1) + i, b = a + seg + 1;
        for (int v : {a, b, a + 1, a + 1, b, b + 1}) m.indices.push_back(v);
    }
    return m;
}
Mesh cube(float s) {
    Mesh m;
    const Vec3 n6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vec3& n : n6) {
        const Vec3 u = std::fabs(n.y) > 0.5f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}, v = cross(n, u);
        const int base = int(m.positions.size());
        for (int k = 0; k < 4; ++k) {
            const float a = (k == 1 || k == 2) ? 1.f : -1.f, b = (k >= 2) ? 1.f : -1.f;
            m.positions.push_back(n * s + u * (a * s) + v * (b * s)); m.normals.push_back(n);
        }
        for (int v2 : {0, 1, 2, 0, 2, 3}) m.indices.push_back(base + v2);
    }
    return m;
}
Scene scene(Vec3 eye) {
    Scene s;
    s.camera.eye = eye; s.camera.center = {0, 0, 0}; s.camera.up = {0, 1, 0};
    s.camera.fovy = FOVY; s.camera.aspect = 1.0f; s.camera.nearZ = 0.1f; s.camera.farZ = 100.0f;
    return s;
}
// The silhouette radius, in pixels, of a sphere of radius r at distance d on the axis.
double circlePx(double r, double d) { return std::tan(std::asin(r / d)) / std::tan(FOVY * 0.5) * (W * 0.5); }

// Precision and coverage of a pixel set against a reference given as a distance function
// and a list of sample points along it.
struct Fit { double worst = 0; int missed = 0, samples = 0, pixels = 0; };
Fit fit(const std::vector<uint8_t>& e, const std::function<double(double, double)>& dist, const std::vector<std::pair<double, double>>& samples,
        double d, const std::function<bool(int, int)>& counts = nullptr) {
    Fit f;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        if (!e[size_t(y) * W + x] || (counts && !counts(x, y))) continue;
        ++f.pixels; f.worst = std::max(f.worst, dist(x + 0.5, y + 0.5));
    }
    for (auto [sx, sy] : samples) {
        ++f.samples;
        bool hit = false;
        for (int y = int(sy - d - 1); y <= int(sy + d + 1) && !hit; ++y) for (int x = int(sx - d - 1); x <= int(sx + d + 1) && !hit; ++x)
            if (x >= 0 && y >= 0 && x < W && y < H && e[size_t(y) * W + x] && std::hypot(x + 0.5 - sx, y + 0.5 - sy) <= d) hit = true;
        if (!hit) ++f.missed;
    }
    return f;
}
std::vector<std::pair<double, double>> circleSamples(double r) {
    std::vector<std::pair<double, double>> s;
    const int n = int(2 * 3.14159265 * r / 0.25);
    for (int k = 0; k < n; ++k) { const double a = 2 * 3.14159265 * k / n; s.push_back({W / 2 + r * std::cos(a), H / 2 + r * std::sin(a)}); }
    return s;
}
bool pass(const Fit& f, double d) { return f.pixels > 0 && f.worst <= d && f.missed == 0; }
Vec3 project(const Scene& s, Vec3 p) {
    const Vec4 c = mul(mul(s.camera.proj(), s.camera.view()), Vec4{p.x, p.y, p.z, 1});
    return {float((c.x / c.w * 0.5 + 0.5) * W), float((1 - (c.y / c.w * 0.5 + 0.5)) * H), c.w};
}
}  // namespace

int main(int argc, char** argv) {
    const bool json = argc > 1 && std::strcmp(argv[1], "--json") == 0;
    const double D = 1.5;
    std::printf(json ? "{\n" : "");
    auto report = [&](const char* name, const Fit& f, const Fit& control) {
        if (json) std::printf(" \"%s\": {\"pixels\": %d, \"worst_px\": %.3f, \"samples\": %d, \"missed\": %d, \"control_fails\": %s},\n",
                              name, f.pixels, f.worst, f.samples, f.missed, pass(control, D) ? "false" : "true");
        else std::printf("%s: %d px, worst %.3f px, %d of %d samples missed; control %s\n", name, f.pixels, f.worst, f.missed, f.samples, pass(control, D) ? "PASSES (bad)" : "fails");
        CHECK(pass(f, D)); CHECK(!pass(control, D));
    };
    // Depth edges: the sphere's silhouette.
    Scene s1 = scene({0, 0, 4}); s1.meshes.push_back(sphere({0, 0, 0}, 1));
    Buffer<uint16_t> ids1; const GBuffer g1 = rasterize(s1, W, H, nullptr, &ids1);
    const double r1 = circlePx(1, 4);
    const auto ring = [](double r) { return [r](double x, double y) { return std::fabs(std::hypot(x - W / 2, y - H / 2) - r); }; };
    const auto de = outline::edges(g1, nullptr, outline::Edge::Depth);
    report("depth_edges", fit(de, ring(r1), circleSamples(r1), D), fit(de, ring(r1 * 1.02), circleSamples(r1 * 1.02), D));
    // Id edges: a larger sphere centred behind it on the axis.
    Scene s2 = scene({0, 0, 4}); s2.meshes.push_back(sphere({0, 0, 0}, 1)); s2.meshes.push_back(sphere({0, 0, -4}, 2.5f));
    Buffer<uint16_t> ids2; const GBuffer g2 = rasterize(s2, W, H, nullptr, &ids2);
    const double r2 = circlePx(2.5, 8);
    const auto ie = outline::edges(g2, &ids2, outline::Edge::Id);
    auto both = [](double a, double b) { return [a, b](double x, double y) { const double q = std::hypot(x - W / 2, y - H / 2); return std::min(std::fabs(q - a), std::fabs(q - b)); }; };
    auto samples2 = circleSamples(r1); for (auto p : circleSamples(r2)) samples2.push_back(p);
    auto bad2 = circleSamples(r1 * 1.02); for (auto p : circleSamples(r2 * 1.02)) bad2.push_back(p);
    report("id_edges", fit(ie, both(r1, r2), samples2, D), fit(ie, both(r1 * 1.02, r2 * 1.02), bad2, D));
    // Normal edges: a cube seen from a corner direction; its three visible creases.
    Scene s3 = scene({2.6f, 2.1f, 3.0f}); s3.meshes.push_back(cube(0.5f));
    const GBuffer g3 = rasterize(s3, W, H);
    const auto ne = outline::edges(g3, nullptr, outline::Edge::Normal);
    const auto creases = [&](Vec3 corner) {
        std::vector<std::pair<Vec3, Vec3>> seg;
        for (Vec3 o : {Vec3{-1, 0, 0}, Vec3{0, -1, 0}, Vec3{0, 0, -1}}) seg.push_back({project(s3, corner), project(s3, corner + Vec3{o.x * corner.x * 2, o.y * corner.y * 2, o.z * corner.z * 2})});
        return seg;
    };
    const auto interior = [&](int x, int y) { for (int j = -2; j <= 2; ++j) for (int i = -2; i <= 2; ++i) { const int a = x + i, b = y + j; if (a < 0 || b < 0 || a >= W || b >= H || !g3.mask.at(a, b)) return false; } return true; };
    const auto segFit = [&](const std::vector<std::pair<Vec3, Vec3>>& seg) {
        const auto dist = [&](double x, double y) { double best = 1e9; for (auto [a, b] : seg) { const double vx = b.x - a.x, vy = b.y - a.y, u = std::clamp(((x - a.x) * vx + (y - a.y) * vy) / (vx * vx + vy * vy), 0.0, 1.0); best = std::min(best, std::hypot(x - a.x - u * vx, y - a.y - u * vy)); } return best; };
        std::vector<std::pair<double, double>> smp;
        for (auto [a, b] : seg) { const double L = std::hypot(b.x - a.x, b.y - a.y); for (double t = 0; t <= L; t += 0.25) { const double x = a.x + (b.x - a.x) * t / L, y = a.y + (b.y - a.y) * t / L; if (interior(int(x), int(y))) smp.push_back({x, y}); } }
        return fit(ne, dist, smp, D, interior);
    };
    report("normal_edges", segFit(creases({0.5f, 0.5f, 0.5f})), segFit(creases({-0.5f, 0.5f, 0.5f})));
    // Inverted hull: the annulus between the silhouettes of radius 1 and 1.05.
    const auto hull = outline::invertedHull(s1, W, H, 0.05f);
    const double ro = circlePx(1.05, 4);
    const auto hullFit = [&](double inner, double outer) {
        Fit f; int inAnn = 0, hitAnn = 0;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const double q = std::hypot(x + 0.5 - W / 2, y + 0.5 - H / 2);
            const bool h = hull[size_t(y) * W + x];
            if (h) { ++f.pixels; f.worst = std::max(f.worst, q < inner ? inner - q : q > outer ? q - outer : 0.0); }
            if (q > inner && q < outer) { ++inAnn; hitAnn += h; }
        }
        f.samples = inAnn; f.missed = inAnn - hitAnn;
        return f;
    };
    const Fit hf = hullFit(r1, ro), hc = hullFit(r1, circlePx(1.10, 4));
    const auto hullPass = [](const Fit& f) { return f.pixels > 0 && f.worst <= 1.0 && f.missed <= 0.01 * f.samples; };
    if (json) std::printf(" \"inverted_hull\": {\"pixels\": %d, \"worst_px\": %.3f, \"annulus_pixels\": %d, \"missed\": %d, \"control_fails\": %s},\n", hf.pixels, hf.worst, hf.samples, hf.missed, hullPass(hc) ? "false" : "true");
    else std::printf("inverted_hull: %d px, worst %.3f px outside the annulus, %d of %d annulus px missed; control %s\n", hf.pixels, hf.worst, hf.missed, hf.samples, hullPass(hc) ? "PASSES (bad)" : "fails");
    CHECK(hullPass(hf)); CHECK(!hullPass(hc));
    // Toon ramp: 4 bands of N.L, against the exact sphere normal at each pixel centre.
    const Vec3 light = normalize(Vec3{-0.5f, -0.6f, -0.62f});
    const auto exactBand = [&](double px, double py, Vec3 L) -> int {
        const double t = std::tan(FOVY * 0.5), x = (px / W * 2 - 1) * t, y = (1 - py / H * 2) * t;
        const double dl = std::sqrt(x * x + y * y + 1), dx = x / dl, dy = y / dl, dz = -1 / dl;
        const double b = 4 * dz, c = 16 - 1, disc = b * b - c;                 // eye (0,0,4), unit sphere
        if (disc < 0) return -1;
        const double s = -b - std::sqrt(disc), nx = dx * s, ny = dy * s, nz = 4 + dz * s;
        const double nl = std::max(0.0, -(nx * L.x + ny * L.y + nz * L.z));
        return std::min(3, int(std::floor(nl * 4)));
    };
    const auto toonFit = [&](Vec3 L) {
        const auto bands = outline::toonBands(g1, light, 4);
        int cov = 0, diff = 0, far = 0;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const int r = bands[size_t(y) * W + x];
            if (r < 0) continue;
            const int e = exactBand(x + 0.5, y + 0.5, L);
            if (e < 0) continue;
            ++cov;
            if (r != e) {
                ++diff; bool near = false;
                for (double oy = -1.5; oy <= 1.5 && !near; oy += 0.5) for (double ox = -1.5; ox <= 1.5 && !near; ox += 0.5) { const int q = exactBand(x + 0.5 + ox, y + 0.5 + oy, L); if (q >= 0 && q != e) near = true; }
                if (!near) ++far;
            }
        }
        return std::tuple<int, int, int>{cov, diff, far};
    };
    const auto [cov, diff, far] = toonFit(light);
    const auto [cov2, diff2, far2] = toonFit(light * -1.0f);
    const bool toonOk = diff <= 0.02 * cov && far == 0, toonCtl = diff2 <= 0.02 * cov2 && far2 == 0;
    if (json) std::printf(" \"toon_ramp\": {\"covered\": %d, \"differ\": %d, \"differ_away_from_a_boundary\": %d, \"control_fails\": %s}\n}\n", cov, diff, far, toonCtl ? "false" : "true");
    else std::printf("toon_ramp: %d of %d px differ, %d away from a boundary; control %s\n", diff, cov, far, toonCtl ? "PASSES (bad)" : "fails");
    CHECK(toonOk); CHECK(!toonCtl);
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
