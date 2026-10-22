// The noise module (evidence/p8-noise-dither-bounds.json n1 to n6): golden hashes per compiler
// family, white noise uniformity, blue noise spectrum, continuity with a step-function control,
// ranges, and fBm's one-octave identity.
//   test_noise
#include "raw/renderer/noise.hpp"
#include "raw/tools/noise_gallery.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
using namespace raw;
using namespace raw::noise;

namespace {
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__EMSCRIPTEN__)
constexpr const char* kFamily = "emscripten";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
std::uint64_t fnv(const std::vector<float>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (float f : v) { std::uint32_t b; std::memcpy(&b, &f, 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255u; h *= 1099511628211ull; } }
    return h;
}
// The whole golden set hashed together, and per image for diagnosis.
struct Pinned { const char* family; std::uint64_t hash; };
constexpr Pinned kPinned[] = {{"gcc", 0xbb69adfcaefa21bcull}, {"msvc", 0xbb69adfcaefa21bcull}};   // recorded 2026-10-21; GCC 13.3 and MSVC (VS 18) agree
// Mean power below 1/8 of Nyquist (DC excluded) of a 64 x 64 field, by a direct DFT.
double lowPower(const std::vector<double>& f) {
    double mean = 0; for (double v : f) mean += v; mean /= double(f.size());
    double sum = 0; int n = 0;
    for (int v = -4; v <= 4; ++v) for (int u = -4; u <= 4; ++u) {
        if ((u == 0 && v == 0) || u * u + v * v > 16) continue;
        double re = 0, im = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const double a = -2.0 * 3.14159265358979323846 * (u * x + v * y) / 64.0, s = f[std::size_t(y) * 64 + x] - mean;
            re += s * std::cos(a); im += s * std::sin(a);
        }
        sum += re * re + im * im; ++n;
    }
    return sum / n;
}
float lipschitz(const std::function<float(float, float, float)>& f, bool threeD) {
    float worst = 0; const float h = 1e-3f;
    std::uint32_t s = 12345u;
    for (int i = 0; i < 200000; ++i) {
        s = hash(s, 1u, 2u, 3u);
        float x = float(s >> 8) / 16777216.0f * 64.0f - 32.0f;
        s = hash(s, 4u, 5u, 6u);
        float y = float(s >> 8) / 16777216.0f * 64.0f - 32.0f, z = threeD ? 0.37f + float(i % 7) : 0.0f;
        if (i % 4 == 0) x = std::floor(x) - h * 0.5f;          // straddle a lattice boundary
        const float d = std::fabs(f(x + h, y, z) - f(x, y, z)) / h, e = std::fabs(f(x, y + h, z) - f(x, y, z)) / h;
        worst = std::max(worst, std::max(d, e));
    }
    return worst;
}
}  // namespace

int main() {
    // n1 goldens.
    std::uint64_t all = 1469598103934665603ull;
    for (const GreyImage& g : noiseGoldens()) {
        const std::uint64_t h = fnv(g.px);
        std::printf("n1 %-26s 0x%016llx\n", g.name.c_str(), static_cast<unsigned long long>(h));
        all ^= h; all *= 1099511628211ull;
    }
    std::printf("n1 %s golden set 0x%016llxull\n", kFamily, static_cast<unsigned long long>(all));
    for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.hash) CHECK(all == p.hash);
    // n2 white: chi-square over 64 bins.
    std::vector<double> bins(64, 0);
    for (int i = 0; i < (1 << 20); ++i) bins[std::size_t(white(i & 1023, i >> 10, 3, 99u) * 64.0f)] += 1;
    double chi = 0; const double e = double(1 << 20) / 64.0;
    for (double b : bins) chi += (b - e) * (b - e) / e;
    std::printf("n2 white chi-square %.1f (63 +- 45)\n", chi);
    CHECK(std::fabs(chi - 63.0) <= 45.0);
    // n3 blue spectrum against white.
    std::vector<double> bl(4096), wh(4096);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { bl[std::size_t(y) * 64 + x] = blue(x, y); wh[std::size_t(y) * 64 + x] = white(x, y, 0, 5u); }
    const double rb = lowPower(bl) / lowPower(wh);
    std::printf("n3 blue / white low-frequency power %.3e (bound 0.10)\n", rb);
    CHECK(rb <= 0.10);
    // n4 continuity, with the step-function control.
    struct L { const char* n; std::function<float(float, float, float)> f; bool d3; };
    const L ls[] = {{"value2", [](float x, float y, float) { return value2(x, y, 1u); }, false},
                    {"value3", [](float x, float y, float z) { return value3(x, y, z, 1u); }, true},
                    {"perlin2", [](float x, float y, float) { return perlin2(x, y, 1u); }, false},
                    {"perlin3", [](float x, float y, float z) { return perlin3(x, y, z, 1u); }, true},
                    {"simplex2", [](float x, float y, float) { return simplex2(x, y, 1u); }, false},
                    {"simplex3", [](float x, float y, float z) { return simplex3(x, y, z, 1u); }, true}};
    for (const L& l : ls) { const float w = lipschitz(l.f, l.d3); std::printf("n4 %-8s worst slope %.2f (bound 16)\n", l.n, w); CHECK(w <= 16.0f); }
    const float ctl = lipschitz([](float x, float y, float) { return white(std::int32_t(std::floor(x)), std::int32_t(std::floor(y)), 0, 1u); }, false);
    std::printf("n4 control (nearest lattice) worst slope %.1f (must exceed 100)\n", ctl);
    CHECK(ctl > 100.0f);
    // n5 ranges.
    float pmin = 1e9f, pmax = -1e9f, vmin = 1e9f, vmax = -1e9f; bool order = true, rangedOk = true;
    for (int i = 0; i < 1000000; ++i) {
        const float x = float(i % 997) * 0.173f - 80.0f, y = float(i / 997) * 0.211f - 90.0f, z = float(i % 13) * 0.29f;
        for (float v : {perlin2(x, y, 2u), perlin3(x, y, z, 2u), simplex2(x, y, 2u), simplex3(x, y, z, 2u)}) { pmin = std::min(pmin, v); pmax = std::max(pmax, v); }
        for (float v : {value2(x, y, 2u), value3(x, y, z, 2u), white(i, i >> 3, 1, 2u)}) { vmin = std::min(vmin, v); vmax = std::max(vmax, v); }
        if (i % 50 == 0) {
            const Cell c = worley3(x, y, z, 2u); order = order && c.f1 <= c.f2;
            const float r = ridged(Base::Simplex, x * 0.1f, y * 0.1f, z, 2u), f = fbm(Base::Perlin, x * 0.1f, y * 0.1f, z, 2u), b = billow(Base::Perlin, x * 0.1f, y * 0.1f, z, 2u);
            rangedOk = rangedOk && r >= 0.0f && r <= 1.0f && f >= -1.05f && f <= 1.05f && b >= -1.05f && b <= 1.05f;
        }
    }
    std::printf("n5 gradient noise [%.3f, %.3f] (bound 1.05), value and white [%.6f, %.6f], worley order %d, octave ranges %d\n", pmin, pmax, vmin, vmax, order, rangedOk);
    CHECK(pmin >= -1.05f && pmax <= 1.05f && vmin >= 0.0f && vmax < 1.0f && order && rangedOk);
    // n6 one octave.
    bool same = true;
    for (int i = 0; i < 1000; ++i) { const float x = i * 0.37f, y = i * 0.11f; same = same && fbm(Base::Perlin, x, y, 0.5f, 4u, {1, 2.0f, 0.5f}) == perlin3(x, y, 0.5f, 4u); }
    std::printf("n6 one-octave fBm equals its base: %d\n", same);
    CHECK(same);
    return raw_test_summary();
}
