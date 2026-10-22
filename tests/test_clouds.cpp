// Volumetric clouds stage 1 (evidence/p14-clouds-bounds.json v1 to v5).
//   test_clouds
#include "raw/renderer/volume.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace raw;
using namespace raw::volume;

namespace {
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
struct Pinned { const char* family; std::uint64_t hash; };
constexpr Pinned kPinned[] = {{"gcc", 0xbdae2b4e79b64567ull}, {"msvc", 0xf8800d9132a6c34dull}};
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
double meanLum(const std::vector<Rgb>& v) { double s = 0; for (const Rgb& c : v) s += lum(c); return s / double(v.size()); }
// Relative RMSE over 8 x 8 blocks (averages per-pixel noise of the path tracer).
double blockRel(const std::vector<Rgb>& a, const std::vector<Rgb>& b, int w, int h) {
    double d2 = 0, s = 0; int n = 0;
    for (int by = 0; by + 8 <= h; by += 8) for (int bx = 0; bx + 8 <= w; bx += 8) {
        double sa = 0, sb = 0;
        for (int y = by; y < by + 8; ++y) for (int x = bx; x < bx + 8; ++x) { sa += lum(a[std::size_t(y) * w + x]); sb += lum(b[std::size_t(y) * w + x]); }
        d2 += (sa - sb) * (sa - sb) / 4096.0; s += sb / 64.0; ++n;
    }
    return std::sqrt(d2 / n) / (s / n);
}
// Mean alpha-gradient magnitude where 0.02 < alpha < 0.98 (the silhouette band).
double edgeGradient(const std::vector<double>& a, int w, int h) {
    double s = 0; int n = 0;
    for (int y = 1; y + 1 < h; ++y) for (int x = 1; x + 1 < w; ++x) {
        const double v = a[std::size_t(y) * w + x];
        if (v <= 0.02 || v >= 0.98) continue;
        const double gx = 0.5 * (a[std::size_t(y) * w + x + 1] - a[std::size_t(y) * w + x - 1]), gy = 0.5 * (a[std::size_t(y + 1) * w + x] - a[std::size_t(y - 1) * w + x]);
        s += std::sqrt(gx * gx + gy * gy); ++n;
    }
    return n ? s / n : 0.0;
}
}  // namespace

int main() {
    MarchDesc md; md.step = 0.05; md.sunSteps = 32;
    // v1 the furnace: albedo 1, uniform environment 1, no sun; and the control at albedo 0.5.
    {
        Cloud c; Lighting l; Camera cam; scene(Scene::Cumulus, c, l, cam);
        c.albedo = 1.0; c.sigma = 3.0; l.uniformEnv = 1.0;
        std::vector<double> var;
        const std::vector<Rgb> f = pathTrace(c, l, cam, 8, 6, 512, false, md, &var, 3);
        double se = 0; for (double v : var) se += v; se = std::sqrt(se) / double(var.size());
        const double m = meanLum(f);
        c.albedo = 0.5;
        const double mc = meanLum(pathTrace(c, l, cam, 8, 6, 128, false, md, nullptr, 3));
        std::printf("v1 furnace: mean %.5f (se %.1e; bound 4 se + 2e-3); control albedo 0.5: %.4f\n", m, se, mc);
        CHECK(std::fabs(m - 1.0) <= 4.0 * se + 2e-3);
        CHECK(mc < 0.97);
    }
    // v2 single scattering: the raymarcher against the path tracer, and a coarse-step control.
    {
        Cloud c; Lighting l; Camera cam; scene(Scene::Cumulus, c, l, cam);
        l.skyOn = false;   // the sun alone, so both see the same single-scattered light
        const int w = 48, h = 27;
        const std::vector<Rgb> rm = march(c, l, cam, w, h, md), pt = pathTrace(c, l, cam, w, h, 512, true, md, nullptr, 5);
        MarchDesc coarse = md; coarse.step = 0.45; coarse.sunSteps = 3;
        const std::vector<Rgb> cm = march(c, l, cam, w, h, coarse);
        const double mr = meanLum(rm), mp = meanLum(pt), mc = meanLum(cm), br = blockRel(rm, pt, w, h), bc = blockRel(cm, pt, w, h);
        std::printf("v2 single scattering: march mean %.3f, path-traced %.3f (%.2f%%), 8x8 blocks %.4f (bounds 3%%, 0.1); coarse control %.2f%%, blocks %.4f\n",
                    mr, mp, 100.0 * (mr / mp - 1.0), br, 100.0 * (mc / mp - 1.0), bc);
        CHECK(std::fabs(mr / mp - 1.0) <= 0.03);
        CHECK(br <= 0.1);
        CHECK(bc > 0.1 || std::fabs(mc / mp - 1.0) > 0.03);
    }
    // v3 goldens.
    {
        std::uint64_t all = 1469598103934665603ull;
        MarchDesc g = md; g.powder = true; g.ambient = true; g.octaves = 3; g.step = 0.06; g.sunSteps = 16;
        for (Scene s : {Scene::Cumulus, Scene::Towering, Scene::Deck, Scene::SmokeSunset}) {
            Cloud c; Lighting l; Camera cam; scene(s, c, l, cam);
            for (const Rgb& v : march(c, l, cam, 96, 54, g, nullptr, 2)) for (double x : {v.r, v.g, v.b}) {
                const unsigned char* q = reinterpret_cast<const unsigned char*>(&x);
                for (std::size_t k = 0; k < sizeof x; ++k) { all ^= q[k]; all *= 1099511628211ull; }
            }
        }
        std::printf("v3 %s goldens 0x%016llxull\n", kFamily, static_cast<unsigned long long>(all));
        for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.hash) CHECK(all == p.hash);
    }
    // v4 multiple scattering: octaves against the path tracer (recorded).
    {
        Cloud c; Lighting l; Camera cam; scene(Scene::Cumulus, c, l, cam);
        l.skyOn = false;
        MarchDesc o = md; o.octaves = 3;
        const double mo = meanLum(march(c, l, cam, 48, 27, o)), mp = meanLum(pathTrace(c, l, cam, 48, 27, 256, false, md, nullptr, 7));
        std::printf("v4 multiple scattering: octaves %.3f, path-traced %.3f (%.1f%%; recorded)\n", mo, mp, 100.0 * (mo / mp - 1.0));
    }
    // v5 the blob review's measure: silhouette sharpness, lobes against an fBm-only blob.
    {
        Cloud c; Lighting l; Camera cam; scene(Scene::Cumulus, c, l, cam);
        std::vector<double> a, b;
        march(c, l, cam, 192, 108, md, &a);
        c.noiseBlob = true;
        march(c, l, cam, 192, 108, md, &b);
        const double ea = edgeGradient(a, 192, 108), eb = edgeGradient(b, 192, 108);
        std::printf("v5 silhouette gradient: lobes %.4f, noise blob %.4f, ratio %.2f (bound 1.5)\n", ea, eb, ea / eb);
        CHECK(ea >= 1.5 * eb);
    }
    return raw_test_summary();
}
