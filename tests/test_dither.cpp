// The dither module (evidence/p8-noise-dither-bounds.json d1 to d4): golden hashes per compiler
// family, the mean on a ramp, the Bayer matrix's structure, and the blue-noise pattern's spectrum.
//   test_dither
#include "raw/renderer/dither.hpp"
#include "raw/renderer/noise.hpp"
#include "raw/tools/noise_gallery.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace raw;
using dither::Method;

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
struct Pinned { const char* family; std::uint64_t hash; };
constexpr Pinned kPinned[] = {{"gcc", 0xb9ca7e2db4a440f7ull}, {"msvc", 0xb9ca7e2db4a440f7ull}};   // recorded 2026-10-21; GCC 13.3 and MSVC (VS 18) agree
double mean(const std::vector<float>& v) { double s = 0; for (float f : v) s += f; return s / double(v.size()); }
double lowPower(const std::vector<float>& f) {
    const double m = mean(f);
    double sum = 0; int n = 0;
    for (int v = -4; v <= 4; ++v) for (int u = -4; u <= 4; ++u) {
        if ((u == 0 && v == 0) || u * u + v * v > 16) continue;
        double re = 0, im = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const double a = -2.0 * 3.14159265358979323846 * (u * x + v * y) / 64.0, s = f[std::size_t(y) * 64 + x] - m;
            re += s * std::cos(a); im += s * std::sin(a);
        }
        sum += re * re + im * im; ++n;
    }
    return sum / n;
}
}  // namespace

int main() {
    // d1 goldens.
    std::uint64_t all = 1469598103934665603ull;
    for (const GreyImage& g : ditherGoldens()) { all ^= fnv(g.px); all *= 1099511628211ull; }
    std::printf("d1 %s golden set 0x%016llxull\n", kFamily, static_cast<unsigned long long>(all));
    for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.hash) CHECK(all == p.hash);
    // d2 the mean on the ramp, two levels.
    const GreyImage ramp = ditherRamp();
    const double m0 = mean(ramp.px);
    for (Method m : {Method::Bayer2, Method::Bayer4, Method::Bayer8, Method::FloydSteinberg, Method::Atkinson, Method::Stucki, Method::BlueNoise, Method::LineFill, Method::ContourFill}) {
        const double d = mean(dither::apply(m, ramp.px, ramp.w, ramp.h)) - m0;
        const bool gated = m == Method::FloydSteinberg || m == Method::Stucki || m == Method::Bayer8 || m == Method::BlueNoise;
        std::printf("d2 %-16s mean error %+.5f%s\n", dither::name(m), d, gated ? " (bound 1/255 = 0.00392)" : " (recorded)");
        if (gated) CHECK(std::fabs(d) <= 1.0 / 255.0);
    }
    // d3 the Bayer matrix.
    std::vector<int> seen(64, 0);
    bool quarters = true;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) ++seen[std::size_t(dither::bayerRank(8, x, y))];
    for (int c : seen) CHECK(c == 1);
    for (int by = 0; by < 8; by += 2) for (int bx = 0; bx < 8; bx += 2) {
        int q[4] = {0, 0, 0, 0};
        for (int y = by; y < by + 2; ++y) for (int x = bx; x < bx + 2; ++x) ++q[dither::bayerRank(8, x, y) / 16];
        quarters = quarters && q[0] == 1 && q[1] == 1 && q[2] == 1 && q[3] == 1;
    }
    std::printf("d3 bayer8 permutation and quarter structure: %d\n", quarters);
    CHECK(quarters);
    CHECK(dither::bayerRank(2, 0, 0) == 0 && dither::bayerRank(2, 1, 0) == 2 && dither::bayerRank(2, 0, 1) == 3 && dither::bayerRank(2, 1, 1) == 1);
    // d4 on flat 50% grey: the blue-noise pattern against white-noise thresholding.
    const std::vector<float> grey(4096, 0.5f);
    const std::vector<float> blue = dither::apply(Method::BlueNoise, grey, 64, 64);
    std::vector<float> wn(4096);
    for (int i = 0; i < 4096; ++i) wn[std::size_t(i)] = noise::white(i & 63, i >> 6, 0, 17u) < 0.5f ? 1.0f : 0.0f;
    const double r = lowPower(blue) / lowPower(wn);
    std::printf("d4 blue-noise pattern / white low-frequency power %.4f (bound 0.10)\n", r);
    CHECK(r <= 0.10);
    return raw_test_summary();
}
