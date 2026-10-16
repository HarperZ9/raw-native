// Properties of the crt-classic, VHS and fibre references that need no exported fixture (restated
// from retro-crt.js's header, vhs.test.mjs and fibre.test.mjs so every platform's ctest runs them).
// The cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_crt_classic.hpp"
#include "raw/renderer/style_fibre.hpp"
#include "raw/renderer/style_vhs.hpp"
#include <cmath>
#include <numeric>
using namespace raw::style;

static void crt() {
    // Every stage off leaves the frame as it was.
    std::vector<std::uint8_t> f(64 * 48 * 4);
    for (std::size_t i = 0; i < f.size(); ++i) f[i] = std::uint8_t((i * 37) & 255);
    std::vector<std::uint8_t> g = f;
    crtClassic(g, 64, 48, ClassicOptions{});
    CHECK(g == f);
    // Each beam bucket keeps the row mean of the retro.py law, 1.05 - strength / 2.
    const std::vector<float> beam = classicBeamTable(4, 0.35, 0.5);
    for (int k = 0; k < 16; ++k) {
        double m = 0;
        for (int r = 0; r < 4; ++r) m += beam[std::size_t(k * 4 + r)];
        CHECK(std::abs(m / 4 - (1.05 - 0.35 / 2)) < 1e-6);
    }
    // An aperture grille lights one channel a column, with the lit gain 1 + 0.45 m.
    const std::vector<float> mt = classicMaskTable("grille", 0.5);
    for (int x = 0; x < 3; ++x) CHECK(std::abs(mt[std::size_t(x * 3 + x)] - 1.225f) < 1e-6f);
}

static void vhs() {
    const VhsPlan plan = resolveVhs(vhsPreset("thriller"), 64, 8);
    for (const auto* t : {&plan.luma, &plan.peak, &plan.chroma}) CHECK(std::abs(std::accumulate(t->begin(), t->end(), 0.0) - 1) < 1e-12);
    // The YIQ inverse undoes the YIQ matrix.
    const auto inv = vhsYiqInverse();
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
        double s = 0;
        for (int k = 0; k < 3; ++k) s += inv[std::size_t(3 * r + k)] * kYiq[std::size_t(3 * k + c)];
        CHECK(std::abs(s - (r == c ? 1 : 0)) < 1e-12);
    }
    // A flat grey with no noise and no dropouts plays back as itself: the filters pass DC and the
    // chroma of a grey is zero.
    VhsParams p = vhsPreset("sp-fresh");
    p.lumaNoise = 0; p.chromaNoise = 0; p.dropouts = 0;
    const VhsPlan quiet = resolveVhs(p, 64, 8);
    std::vector<float> grey(64 * 8 * 4, 0.4f);
    const std::vector<float> sig = vhsFrame(quiet, grey, 3);
    for (std::size_t i = 0; i < sig.size(); i += 4) for (int c = 0; c < 3; ++c) CHECK(std::abs(sig[i + std::size_t(c)] - 0.4f) < 1e-5f);
}

static void fibre() {
    // A uniform wash on paper with no fibres stays uniform: there is no gradient to carry pigment,
    // and deposition (the same everywhere without crossings) only moves it from suspended to
    // deposited, which the output reads together. With fibres, deposition varies with the crossing
    // density, so the suspended pigment does not stay uniform.
    FibreParams p = fibrePreset("rough");
    p.perCell = 0;
    const FibrePlan plan = resolveFibre(p, 40, 30);
    std::vector<float> flat(40 * 30 * 4, 0.5f);
    const std::vector<double> out = runFibre(plan, flat);
    for (std::size_t i = 0; i < out.size(); i += 4) for (int c = 0; c < 3; ++c) CHECK(std::abs(out[i + std::size_t(c)] - 0.5) < 1e-9);
}

int main() {
    crt(); vhs(); fibre();
    return raw_test_summary();
}
