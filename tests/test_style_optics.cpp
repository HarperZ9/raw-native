// Properties of the film and lens-suite references that need no exported fixture (restated from
// web/shaders/film.test.mjs and lens/lens.test.mjs so every platform's ctest runs them). The
// cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_film.hpp"
#include "raw/renderer/style_lens.hpp"
#include <cmath>
using namespace raw::style;

static void film() {
    // The colour timing prints a scene 18% grey to a neutral 0.18: with no grain and no halation, a
    // flat grey frame comes out at 0.18 in every channel (the f32 stores aside).
    FilmParams p = filmPreset("500t-print");
    p.grainAmount = 0; p.halationReach = {0, 0, 0};
    const FilmPlan plan = resolveFilm(p, 32, 18, 32, 18);
    const std::vector<float> grey(32 * 18 * 4, 0.18f);
    const std::vector<float> img = filmFrame(plan, grey, 0);
    for (std::size_t i = 0; i < img.size(); i += 4) for (int c = 0; c < 3; ++c) CHECK(std::abs(img[i + std::size_t(c)] - 0.18) < 1e-5);
    // The Poisson thresholds of each level rise with the count (a CDF).
    const auto& P = filmPoissonTable();
    for (std::size_t m = 0; m < 512; ++m) for (std::size_t k = 1; k < 16; ++k) CHECK(P[m * 16 + k] >= P[m * 16 + k - 1]);
}

static void lens() {
    // Each channel's bin weights sum to 1, so a wavelength-independent term returns the source's RGB.
    const LensBin3 w = lensBinRgb();
    for (int k = 0; k < 3; ++k) { double s = 0; for (const auto& b : w) s += b[std::size_t(k)]; CHECK(std::abs(s - 1) < 1e-12); }
    // The prescription's focal length (prescription.mjs: 43.0 mm as drawn, 42.80 mm with the catalogue
    // N-BK7 and N-SF2 glasses) and its 15 ghost pairs.
    const LensGhostTable gt = lensGhostTable();
    CHECK(std::abs(gt.f - 42.80) < 0.01);
    CHECK(gt.m.size() == 15);
    // A quarter-wave coating at its design wavelength and normal incidence: the closed form.
    const LensCoating c;
    const double closed = std::pow((c.ns - c.n1 * c.n1) / (c.ns + c.n1 * c.n1), 2);
    CHECK(std::abs(lensFilmReflectance(c.quarterAt, 0, c) - closed) < 1e-12);
    // The starburst table sums to 1 in each channel.
    const LensStar st = lensStarburst(LensIris{});
    for (int k = 0; k < 3; ++k) { double s = 0; for (std::size_t i = std::size_t(k); i < st.data.size(); i += 3) s += st.data[i]; CHECK(std::abs(s - 1) < 1e-9); }
}

int main() {
    film(); lens();
    return raw_test_summary();
}
