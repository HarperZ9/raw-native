// Properties of the physical CRT reference that need no exported fixture (restated from
// web/shaders/crt.test.mjs so every platform's ctest runs them). The cross-language comparison is
// raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_crt.hpp"
#include <array>
#include <cmath>
#include <vector>
using namespace raw::style;

namespace {
std::vector<float> flat(int w, int h, double r, double g, double b) {
    std::vector<float> d(std::size_t(w) * h * 4);
    for (std::size_t i = 0; i < d.size(); i += 4) { d[i] = float(r); d[i + 1] = float(g); d[i + 2] = float(b); d[i + 3] = 1; }
    return d;
}
}  // namespace

int main() {
    // A colour screen at full drive is the white point at Y = 1: in Rec.709 out, (1, 1, 1).
    const CrtScreen p22 = crtScreen("p22");
    for (int r = 0; r < 3; ++r) CHECK(std::abs(p22.matrix[std::size_t(r * 3)] + p22.matrix[std::size_t(r * 3 + 1)] + p22.matrix[std::size_t(r * 3 + 2)] - 1) < 1e-9);
    // Halo: energy is conserved and the ring starts at 2 t tan(theta_c).
    CrtParams g;
    const CrtHalo k = crtHaloKernel(g, 0.5, 25);
    double sum = k.direct;
    for (float w : k.weights) sum += w;
    CHECK(std::abs(sum - 1) < 1e-6);
    CHECK(std::abs(k.ringMm - 2 * 12 * std::tan(std::asin(1 / 1.52))) < 1e-9);
    // RGB with the bandwidth off is the source, sample for sample.
    {
        CrtParams p = crtPreset("pvm-20");
        p.rgbMHz = 0;
        const CrtPlan plan = resolveCrt(p, 32, 8, 64, 32);
        std::vector<float> src = flat(32, 8, 0, 0, 0);
        for (int x = 0; x < 32; ++x) src[(std::size_t(3) * 32 + x) * 4] = float((x % 5) / 4.0);
        const std::vector<float> sig = crtSignal(plan, src, 0);
        bool same = true;
        for (int kk = 0; kk < plan.N; ++kk) same = same && std::abs(sig[(std::size_t(3) * plan.N + kk) * 4] - src[(std::size_t(3) * 32 + (2 * kk + 1) * 32 / (2 * plan.N)) * 4]) < 1e-7;
        CHECK(same);
    }
    // Composite decodes a flat colour back to itself, and grey carries no chroma, with either decoder.
    for (bool comb : {true, false}) {
        const double colours[2][3] = {{0.5, 0.5, 0.5}, {0.7, 0.3, 0.2}};
        for (const auto& rgb : colours) {
            const double c0 = rgb[0], c1 = rgb[1], c2 = rgb[2];
            CrtParams p = crtPreset("trinitron-tv");
            p.comb = comb;
            const CrtPlan plan = resolveCrt(p, 64, 8, 64, 32);
            const std::vector<float> sig = crtSignal(plan, flat(64, 8, c0, c1, c2), 0);
            const std::size_t i = (std::size_t(4) * plan.N + std::size_t(plan.N >> 1)) * 4;
            CHECK(std::abs(sig[i] - c0) < 0.01 && std::abs(sig[i + 1] - c1) < 0.01 && std::abs(sig[i + 2] - c2) < 0.01);
        }
    }
    // Mask transmission averages to 1 over the screen, for each mask.
    for (int type : {1, 2, 3}) {
        CrtParams p = crtPreset("pvm-20");
        p.maskType = type;
        p.pitch = 0.6;
        const CrtPlan plan = resolveCrt(p, 64, 48, 96, 72);
        double acc[3] = {0, 0, 0};
        long n = 0;
        for (double y = -40; y < 40; y += 0.37)
            for (double x = -60; x < 60; x += 0.29) {
                const auto v = crtMaskCover(plan, x, y, 0.05, 0.05, 0, 0);
                for (int c = 0; c < 3; ++c) acc[c] += v[std::size_t(c)];
                ++n;
            }
        for (double a : acc) CHECK(std::abs(a / double(n) - 1) < 0.03);
        CHECK(crtMaskMean(p) > 0 && crtMaskMean(p) < 1);
    }
    return raw_test_summary();
}
