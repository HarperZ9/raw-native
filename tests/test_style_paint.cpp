// Properties of the painterly reference that need no exported fixture (restated from
// web/shaders/paint.test.mjs so every platform's ctest runs them). The cross-language comparison
// is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_paint.hpp"
#include <cmath>
#include <vector>
using namespace raw::style;

namespace {
std::vector<float> wave(int W, int H, int dx) {
    std::vector<float> d(std::size_t(W) * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double u = x + dx;
            float* p = &d[(std::size_t(y) * W + x) * 4];
            p[0] = float(0.3 + 0.25 * std::sin(u * 0.21) * std::cos(y * 0.17));
            p[1] = float(0.25 + 0.2 * std::sin(u * 0.13 + y * 0.11));
            p[2] = float(0.2 + 0.15 * std::cos(u * 0.09 - y * 0.2));
            p[3] = 1;
        }
    return d;
}
// Mean frame-to-frame difference of a one-pixel pan, the second frame shifted back.
double panFlicker(bool anchored) {
    const int W = 72, H = 40;
    auto out = [&](int dx) {
        PaintParams p = paintPreset("oil-grotesque");
        p.canvasOffset = {anchored ? double(dx) : 0.0, 0.0};
        return runPaint(resolvePaint(p, W, H), wave(W, H, dx), nullptr);
    };
    const std::vector<float> a = out(0), b = out(1);
    double s = 0;
    long n = 0;
    for (int y = 12; y < H - 12; ++y)
        for (int x = 12; x < W - 13; ++x)
            for (int c = 0; c < 3; ++c) { s += std::abs(double(a[(std::size_t(y) * W + x + 1) * 4 + c]) - b[(std::size_t(y) * W + x) * 4 + c]); ++n; }
    return s / double(n);
}
}  // namespace

int main() {
    // Value noise stays in [0, 1].
    for (int i = 0; i < 200; ++i) {
        const double v = paintVnoise(i * 0.37 - 20, i * 0.53 - 31, 61u);
        CHECK(v >= 0 && v <= 1);
    }
    // A 2D pan through motion vectors moves the canvas exactly and never wakes the second layer.
    {
        const int W = 48, H = 32;
        PaintCanvas st(W, H);
        std::vector<float> still(std::size_t(W) * H * 2, 0.0f), mv(std::size_t(W) * H * 2, 0.0f), d(std::size_t(W) * H, 5.0f);
        for (int i = 0; i < W * H; ++i) mv[std::size_t(i) * 2] = 2;
        paintDecide(st);
        paintAdvect(st, still, d, d);
        const std::vector<float> C0 = st.C;
        for (int f = 0; f < 10; ++f) { paintDecide(st); paintAdvect(st, mv, d, d); }
        int checked = 0;
        bool exact = true;
        for (int y = 0; y < H; ++y)
            for (int x = 22; x < W; ++x) {
                exact = exact && std::abs(st.C[(std::size_t(y) * W + x) * 4] - C0[(std::size_t(y) * W + x - 20) * 4]) <= 1e-3;
                ++checked;
            }
        CHECK(exact);
        CHECK(checked > 500);
        bool yKept = true;
        for (int y = 4; y < H - 4; ++y) yKept = yKept && std::abs(st.C[(std::size_t(y) * W + 40) * 4 + 1] - C0[(std::size_t(y) * W + 40) * 4 + 1]) <= 1e-3;
        CHECK(yKept);
        CHECK(st.layer[1].age == -1);
    }
    // Panning: with the canvas anchored, consecutive frames agree far more than without.
    const double fixed = panFlicker(true), swim = panFlicker(false);
    CHECK(fixed < 0.25 * swim);
    CHECK(fixed < 0.01);
    return raw_test_summary();
}
