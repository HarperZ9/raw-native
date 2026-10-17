// Properties of the sag and caricature references that need no exported fixture (restated from
// web/shaders/sag.test.mjs and caricature.test.mjs so every platform's ctest runs them). The
// cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_sag.hpp"
#include <cmath>
using namespace raw::style;

static void sag() {
    // A black frame draws no current: no sag, every line where it belongs, the frame stays black.
    const SagPlan plan = resolveSag(sagPreset("thriller"), 64, 48);
    std::vector<float> black(64 * 48 * 4, 0.0f);
    for (std::size_t i = 3; i < black.size(); i += 4) black[i] = 1;
    SagScan sc;
    const std::vector<float> out = runSag(plan, black, sc);
    for (std::size_t i = 0; i < out.size(); i += 4) for (int c = 0; c < 3; ++c) CHECK(out[i + std::size_t(c)] == 0.0f);
    for (double v : sc.v) CHECK(std::abs(v) <= 1e-12);
    // A bright frame: the drawn line heights still rise strictly from top to bottom.
    std::vector<float> lit(64 * 48 * 4, 0.9f);
    const std::vector<float> o2 = runSag(plan, lit, sc);
    (void)o2;
    for (std::size_t y = 1; y < sc.Y.size(); ++y) CHECK(sc.Y[y] > sc.Y[y - 1]);
}

static void caricature() {
    // No salience, no warp: the output is the input.
    const CariPlan plan = resolveCaricature(cariPreset("grotesque"), 40, 30);
    std::vector<float> src(40 * 30 * 4);
    for (std::size_t i = 0; i < src.size(); ++i) src[i] = float((i * 13) % 17) / 17.0f;
    const std::vector<std::int32_t> none(40 * 30, -1);
    CariFields F;
    const std::vector<float> out = runCaricature(plan, src, none, F);
    CHECK(F.lambda == 0);
    for (std::size_t i = 0; i < out.size(); ++i) if ((i & 3) != 3) CHECK(std::abs(out[i] - src[i]) <= 1e-6f);
}

int main() {
    sag(); caricature();
    return raw_test_summary();
}
