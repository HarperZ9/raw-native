// The colonnade (evidence/colonnade-bounds.json co1 to co4): the sky on escaped camera rays, the
// emissive sphere's colour bleed, a sun shadow, and the frame's hash per compiler family.
//   test_colonnade
#include "raw/renderer/rt_colonnade.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace raw;
using namespace raw::rt;

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
constexpr Pinned kPinned[] = {{"gcc", 0xe4715df692d0642eull}, {"msvc", 0x21722eb16bd5dfa5ull}};   // recorded 2026-10-21
double lum3(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
// The radiance at one world point, seen from `eye` through a one-pixel camera.
PathTraceOutput at(const PtScene& s, Vec3 eye, Vec3 p, std::uint32_t spp) {
    PathTraceDesc d; d.width = d.height = 1; d.spp = spp; d.jitter = false; d.seed = 9; d.maxBounces = 8;
    d.camera = d.previous = {eye, p, {0, 1, 0}, 0.002f};
    return pathTraceCpu(s, d, 0);
}
bool visible(const PtScene& s, const std::vector<Tri>& tris, Vec3 a, Vec3 b) {
    const Vec3 d = b - a; const float l = length(d);
    return !occluded(s.tree, tris, {a, d * (1.0f / l)}, l * 0.999f);
}
}  // namespace

int main() {
    // co1 the sky on escaped camera rays (noon), no jitter.
    {
        const PtScene s = ptColonnade({});
        PathTraceDesc d; d.width = 48; d.height = 27; d.spp = 1; d.jitter = false; d.seed = 1;
        d.camera = d.previous = {{6.5f, 1.7f, 7.5f}, {-0.3f, 4.5f, -0.5f}, {0, 1, 0}, 1.0f};
        const PathTraceOutput o = pathTraceCpu(s, d, 0);
        const Vec3 f = normalize(d.camera.target - d.camera.eye), sx = normalize(cross(f, d.camera.up)), u = cross(sx, f);
        const float th = std::tan(0.5f), aspect = 48.0f / 27.0f;
        double worst = 0; int n = 0;
        for (int y = 0; y < 27; ++y) for (int x = 0; x < 48; ++x) {
            const std::size_t i = std::size_t(y) * 48 + x;
            if (o.triangle[i] >= 0) continue;
            const float nx = (x + 0.5f) / 48.0f * 2.0f - 1.0f, ny = 1.0f - (y + 0.5f) / 27.0f * 2.0f;
            const Vec3 dir = normalize(f + sx * (nx * th * aspect) + u * (ny * th));
            const lighting::Rgb e = s.environment.sample(0, {dir.x, dir.y, dir.z});
            const double want = 0.2126 * e.r + 0.7152 * e.g + 0.0722 * e.b, got = lum3(o.radiance.data() + 3 * i);
            worst = std::max(worst, std::fabs(got - want) / want); ++n;
        }
        std::printf("co1 sky on %d escaped pixels: worst relative %.2e (bound 1e-5)\n", n, worst);
        CHECK(n > 50);
        CHECK(worst <= 1e-5);
    }
    // co2 colour bleed beside the emissive sphere at sunset.
    {
        ColonnadeParams on; on.sunElevation = 2.0; ColonnadeParams off = on; off.emission = false;
        const Vec3 eye{0.3f, 1.2f, 3.2f}, p{0.3f, 0.0f, 1.62f};
        const PathTraceOutput a = at(ptColonnade(on), eye, p, 2048), b = at(ptColonnade(off), eye, p, 2048);
        const double ra = a.radiance[0] / a.radiance[2], rb = b.radiance[0] / b.radiance[2];
        std::printf("co2 floor red/blue beside the sphere: emission on %.3f, off %.3f, rise %.1f%% (bound 10%%)\n", ra, rb, 100.0 * (ra / rb - 1.0));
        CHECK(ra >= 1.10 * rb);
    }
    // co3 a sun shadow at golden hour: floor points behind the colonnade, one occluded and one lit.
    {
        ColonnadeParams g; g.sunElevation = 10.0;
        const PtScene s = ptColonnade(g);
        const std::vector<Tri> tris = trianglesOf(s.geo);
        const Vec3 sun = colonnadeSun(g), eye{1.0f, 2.2f, 4.5f};
        Vec3 shade{0, 0, 0}, lit{0, 0, 0}; bool hs = false, hl = false;
        for (int i = 0; i < 400 && !(hs && hl); ++i) {
            const Vec3 q{-3.0f + 0.015f * float(i), 0.001f, -1.6f};
            if (!visible(s, tris, eye, q + Vec3{0, 0.01f, 0})) continue;
            const bool sunlit = visible(s, tris, q + Vec3{0, 0.01f, 0}, q + sun * 100.0f);
            if (!sunlit && !hs) { shade = q; hs = true; }
            if (sunlit && !hl && hs) { lit = q; hl = true; }
        }
        CHECK(hs && hl);
        if (hs && hl) {
            const double ys = lum3(at(s, eye, shade, 1024).radiance.data()), yl = lum3(at(s, eye, lit, 1024).radiance.data());
            std::printf("co3 golden hour floor: shadow %.1f, lit %.1f nits, ratio %.3f (bound 0.5; recorded, not gated: addendum A)\n", ys, yl, ys / yl);
            CHECK(ys < yl);   // a shadow, however shallow
        }
    }
    // co4 the frame's hash.
    {
        const PtScene s = ptColonnade({});
        PathTraceDesc d; d.width = 64; d.height = 36; d.spp = 4; d.seed = 3;
        d.camera = d.previous = colonnadeCamera();
        const PathTraceOutput o = pathTraceCpu(s, d, 2);
        std::uint64_t h = 1469598103934665603ull;
        for (float v : o.radiance) { std::uint32_t b; std::memcpy(&b, &v, 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255u; h *= 1099511628211ull; } }
        std::printf("co4 %s frame hash 0x%016llxull\n", kFamily, static_cast<unsigned long long>(h));
        for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.hash) CHECK(h == p.hash);
    }
    return raw_test_summary();
}
