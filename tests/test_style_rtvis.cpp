// Properties of the one-ray visibility estimator (restated from web/shaders/ditherlight/rtvis.test.mjs
// so every platform's ctest runs them). The cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_rtvis.hpp"
#include <set>
#include <string>
using namespace raw::style;

int main() {
    // The 64 rank points form a (0, 6, 3)-net in base 2: every elementary box holds exactly one.
    int shapes = 0;
    for (int a = 0; a <= 6; ++a) for (int b = 0; a + b <= 6; ++b) {
        const int c = 6 - a - b;
        std::set<std::string> seen;
        for (int r = 0; r < 64; ++r) seen.insert(std::to_string(r >> (6 - a)) + "," + std::to_string(rtvisBitrev6(r) >> (6 - b)) + "," + std::to_string(rtvisPascal6(r) >> (6 - c)));
        CHECK(seen.size() == 64);
        shapes++;
    }
    CHECK(shapes == 28);
    // The concentric map keeps every cell centre in the unit disc.
    for (int j = 0; j < 64; ++j) for (int i = 0; i < 64; ++i) { const auto p = rtvisConcentric((i + 0.5) / 64, (j + 0.5) / 64); CHECK(p[0] * p[0] + p[1] * p[1] <= 1 + 1e-12); }
    // Visibility is 1 in the open and 0 deep in the wall's shadow.
    const YardLight L;
    const YardBlocker wall = yardBlocker("wall-x");
    CHECK(rtvisVisibility("ref64", L, {0, 0, -2}, 0, 0, 0, wall) == 1);
    CHECK(rtvisVisibility("ref64", L, {0, 0, 1.0}, 0, 0, 0, wall) == 0);
    return raw_test_summary();
}
