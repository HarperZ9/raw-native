// Properties of the pixel-art reference that need no exported fixture (restated from
// web/shaders/pixel.test.mjs so every platform's ctest runs them). The cross-language comparison
// is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_pixel.hpp"
#include <cmath>
using namespace raw::style;

int main() {
    // The snapped origin belongs to the world: every eye inside one cell gives the same origin.
    const PV3 a = pixelSnapOrigin({0.31, 1.04, 2.49}, 0.5), b = pixelSnapOrigin({0.49, 1.01, 2.26}, 0.5);
    CHECK(a == b);
    // A probe's colours do not depend on the camera: two views inside a cell use the same probe, so
    // a texel that both views show has the same colour (the frame loop keeps one probe a cell).
    PixelOptions o; o.N = 16;
    PixelRunner run(o, 48, 30);
    const PixelRunner::Frame f1 = run.frame({0.31, 4.2, 6.49}, {0, 0.6, 0}, 0.75);
    const PV3 origin1 = f1.probe->origin;
    const PixelRunner::Frame f2 = run.frame({0.33, 4.2, 6.47}, {0, 0.6, 0}, 0.75);
    CHECK(f2.probe->origin == origin1);
    // The posterise step lands on a band: a stylised grey's OKLab lightness is a multiple of 1 / bands.
    const PV3 g = pixelStylise({0.18, 0.18, 0.18}, 6, 0);
    const double L = std::cbrt(0.4122214708 * g[0] + 0.5363325363 * g[1] + 0.0514459929 * g[2]) * 0.2104542553 +
                     std::cbrt(0.2119034982 * g[0] + 0.6806995451 * g[1] + 0.1073969566 * g[2]) * 0.7936177850 -
                     std::cbrt(0.0883024619 * g[0] + 0.2817188376 * g[1] + 0.6299787005 * g[2]) * 0.0040720468;
    CHECK(std::abs(L * 6 - std::round(L * 6)) < 1e-6);
    return raw_test_summary();
}
