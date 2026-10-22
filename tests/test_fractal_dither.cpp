// Fractal dither (evidence/fractal-dither-bounds.json f3, f4, f7): the construction's properties,
// scale invariance, the orbit scene and the golden hashes. The orbit and bootstrap results are
// raw_native_cli fractal-dither orbit.
#include "check.hpp"
#include "raw/renderer/fractal_metrics.hpp"
#include "raw/renderer/fractal_scene.hpp"
#include "raw/tools/fractal_cmd.hpp"
#include <cstdio>
using namespace raw;
using namespace raw::style;

static void structure() {
    // rank_K at K = 3 is the 8 x 8 Bayer rank.
    for (int y = -9; y < 17; ++y) for (int x = -9; x < 17; ++x) CHECK(fractalRank(x, y, 3) == bayer8Rank(x, y));
    // The blend is the coarse threshold at m = 0 and the fine one at m = 1, over every rank pair of a sub-cell.
    for (int rc = 0; rc < 4096; rc += 37)
        for (int d = 0; d < 4; ++d) {
            const double tc = (rc + 0.5) / 4096, tf = (d * 1024 + rc / 4 + 0.5) / 4096;
            CHECK_NEAR(fractalBlend(tc, tf, 0.0), tc, 1e-12);
            CHECK_NEAR(fractalBlend(tc, tf, 1.0), tf, 1e-12);
        }
    // Uniform marginal on a chart, at several blend weights (16 bins within 3% of 1/16).
    for (double m : {0.25, 0.5, 0.75}) {
        long bin[16] = {};
        for (int j = 0; j < 512; ++j)
            for (int i = 0; i < 512; ++i) {
                const double tf = (fractalRank(i, j) + 0.5) / 4096, tc = (fractalRank(i >> 1, j >> 1) + 0.5) / 4096;
                bin[std::min(15, int(fractalBlend(tc, tf, m) * 16))]++;
            }
        for (long b : bin) CHECK_NEAR(double(b) / (512.0 * 512.0), 1.0 / 16, 0.03 / 16);
    }
    // Dots are added, not removed, as the pattern grows: a coarse cell lit at brightness b keeps its corner sub-cell lit.
    long coarseLit = 0, cornerKept = 0;
    for (int J = 0; J < 64; ++J)
        for (int I = 0; I < 64; ++I) {
            const double tc = (fractalRank(I, J) + 0.5) / 4096, tf = (fractalRank(2 * I, 2 * J) + 0.5) / 4096;
            for (double b : {0.1, 0.3, 0.5, 0.7, 0.9}) if (b > tc) { ++coarseLit; cornerKept += b > tf; }
        }
    CHECK(coarseLit > 0 && cornerKept == coarseLit);
}

static void scale() {
    std::string json;
    CHECK(fractal::scaleReport(json) == 0);
}

static std::uint64_t levelHash(const char* preset, Anchor a) {
    const fractal::Frame f = fractal::renderGBuffer(fractal::orbitCamera(0), 256, 144);
    const DitherFrame o = runDitherlight(fractal::orbitPlan(preset, a, 144, 2.0), fractal::lights(), f.g);
    return fractal::fnv1a(o.level);
}
static void scene() {
    const fractal::Frame a = fractal::renderGBuffer(fractal::orbitCamera(0), 128, 72), b = fractal::renderGBuffer(fractal::orbitCamera(1), 128, 72);
    const fractal::Motion m = fractal::motion(b, a);
    long surf = 0, valid = 0;
    for (std::size_t i = 0; i < b.id.size(); ++i) { surf += b.id[i] != 0; valid += m.valid[i]; }
    CHECK(surf > 128 * 72 * 9 / 10);          // the courtyard fills the frame
    CHECK(valid > surf * 8 / 10);             // most history is valid one degree on
    CHECK(fractal::orbitCamera(0).eye[1] == 4.2);
}
static void goldens() {
    // Pinned level-plane hashes (FNV-1a) of orbit frame 0 at 256 x 144.
    CHECK(levelHash("ink", Anchor::Fractal) == 0x5ac361b7dfe0a6bbull);
    CHECK(levelHash("tones4", Anchor::Fractal) == 0x922c22c7c711d410ull);
    CHECK(levelHash("ink", Anchor::Screen) == 0x31c2b5c49d02867dull);
}

int main() {
    structure(); scale(); scene(); goldens();
    return raw_test_summary();
}
