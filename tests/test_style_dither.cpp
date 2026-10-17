// Properties of the palette dither and development adjacency references that need no exported
// fixture (web/shaders/dither.test.mjs and adjacency.test.mjs, restated so every platform's ctest
// runs them). The cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_adjacency.hpp"
#include "raw/renderer/style_dither.hpp"
#include <cmath>
using namespace raw::style;

static void dither() {
    // A plate of exact palette colours maps to those entries in every mode, mix modes included.
    const DitherPalette pal = ditherPalette("pico8");
    std::vector<std::uint8_t> plate;
    for (const PaletteEntry& e : pal.entries) plate.insert(plate.end(), {std::uint8_t(e.rgb[0]), std::uint8_t(e.rgb[1]), std::uint8_t(e.rgb[2]), 255});
    for (const char* m : {"none", "bayer4", "bayer8", "noise", "blue", "mix-bayer4"}) {
        const auto idx = ditherPlate(plate, int(pal.entries.size()), 1, pal, {ditherMode(m), 1, 0});
        for (std::size_t k = 0; k < idx.size(); ++k) CHECK(idx[k] == k);
    }
    // A mid grey between black and white on a two-entry palette dithers to about half white.
    const DitherPalette bw = ditherPalette("mono1");
    std::vector<std::uint8_t> grey(64 * 64 * 4, 255);
    for (std::size_t i = 0; i < grey.size(); i += 4) grey[i] = grey[i + 1] = grey[i + 2] = 128;
    const auto g = ditherPlate(grey, 64, 64, bw, {DitherMode::Bayer8, 1, 0});
    int ones = 0;
    for (auto v : g) ones += v;
    CHECK(ones > 0 && ones < 64 * 64);
}
static void adjacency() {
    // A flat field develops the same everywhere, so the output equals the input in shape.
    const AdjPlan plan = resolveAdjacency(adjPreset("eberhard"), 48, 32);
    std::vector<float> flat(48 * 32 * 4, 0.4f);
    const std::vector<double> out = runAdjacency(plan, flat);
    double lo = 1e9, hi = -1e9;
    for (std::size_t i = 0; i < out.size(); i += 4) { lo = std::min(lo, out[i]); hi = std::max(hi, out[i]); }
    CHECK(hi - lo <= 1e-9);
}

int main() {
    dither(); adjacency();
    return raw_test_summary();
}
