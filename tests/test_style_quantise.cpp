// Properties of the hysteresis and palette-locked lighting references that need no exported
// fixture (web/shaders/hysteresis.test.mjs and palettelight.test.mjs, restated so every platform's
// ctest runs them). The cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_quantise.hpp"
#include <cmath>
using namespace raw::style;

static std::vector<float> ramp(int w, int h, double scale) {
    std::vector<float> v(std::size_t(w) * h * 4, 1.0f);
    for (int i = 0; i < w * h; ++i) for (int c = 0; c < 3; ++c) v[std::size_t(i) * 4 + std::size_t(c)] = float(scale * (i % w) / w * (0.8 + 0.1 * c));
    return v;
}
static void hysteresis() {
    const int w = 32, h = 4;
    const std::vector<float> dist(std::size_t(w) * h, 5.0f), none;
    // Zero margin keeps no decision: every frame equals the plain decision (a fresh state each time).
    HystPlan p = hystPreset("bands6");
    p.margin = 0;
    HystState held, fresh;
    for (int f = 0; f < 4; ++f) {
        const std::vector<float> img = ramp(w, h, 1 + 0.05 * f);
        HystState first;
        CHECK(hystStep(p, w, h, img, none, none, dist, held).idx == hystStep(p, w, h, img, none, none, dist, first).idx);
    }
    // A static sequence equals the plain decision at any margin.
    HystPlan q = hystPreset("pico8");
    HystState s;
    const std::vector<float> img = ramp(w, h, 1.5);
    HystState plainState;
    const auto plain = hystStep(q, w, h, img, none, none, dist, plainState).idx;
    for (int f = 0; f < 3; ++f) CHECK(hystStep(q, w, h, img, none, none, dist, s).idx == plain);
}
static void palettelight() {
    const PalettelightPlan p = palettelightPlan({5.2, 3.6, 5.6});
    CHECK(int(p.palette.entries.size()) == 8 * 4 + 6 + 3 + 2 + 1);
    ChapelGBuffer g;
    g.w = 4; g.h = 2;
    g.hit = {0, 1, 1, 1, 1, 1, 1, 1}; g.cls = {0, 0, 1, 2, 3, 4, 5, 6};
    g.a.assign(8, 0.6f); g.phase.assign(8, 0.3f); g.P.assign(24, 1.0f); g.vis.assign(24, 1.0f);
    for (int i = 0; i < 8; ++i) { g.N.push_back(0); g.N.push_back(1); g.N.push_back(0); }
    const PalettelightFrame f = runPalettelight(p, g, 0.4);
    CHECK(f.idx[0] == p.palette.night);
    for (int i = 0; i < 8; ++i) {
        const auto& e = p.palette.entries[std::size_t(f.idx[std::size_t(i)])].rgb;
        for (std::size_t c = 0; c < 3; ++c) CHECK(f.rgba[std::size_t(i) * 4 + c] == e[c]);   // every pixel is a palette entry
    }
}

int main() {
    hysteresis(); palettelight();
    return raw_test_summary();
}
