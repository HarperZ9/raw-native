// The fractal dither's own measures: see raw/renderer/fractal_metrics.hpp.
#include "raw/renderer/fractal_metrics.hpp"
#include <algorithm>
#include <cmath>
namespace raw::fractal {

double accuracyA(const std::vector<double>& outLinear, const std::vector<double>& tone, const std::vector<std::uint8_t>& valid, int w, int h) {
    double sum = 0;
    long n = 0;
    for (int by = 0; by + 4 <= h; by += 4)
        for (int bx = 0; bx + 4 <= w; bx += 4) {
            double so = 0, st = 0;
            bool all = true;
            for (int y = by; y < by + 4 && all; ++y)
                for (int x = bx; x < bx + 4; ++x) {
                    const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
                    if (!valid[i]) { all = false; break; }
                    so += outLinear[i]; st += tone[i];
                }
            if (!all) continue;
            sum += std::abs(std::cbrt(so / 16) - std::cbrt(st / 16));
            ++n;
        }
    return n > 0 ? 100.0 * sum / double(n) : -1.0;
}

double changeDensity(const std::vector<std::uint8_t>& level, int w, int h) {
    long changed = 0, total = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x + 1 < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            changed += level[i] != level[i + 1];
            ++total;
        }
    return total > 0 ? double(changed) / double(total) : 0.0;
}

}  // namespace raw::fractal
