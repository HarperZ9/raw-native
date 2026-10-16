// OKLab and the retro palettes: see raw/renderer/style_oklab.hpp. Operations are in the order of
// web/shaders/reference/contracts.mjs and retro-palettes.mjs.
#include "raw/renderer/style_oklab.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace raw::style {

double srgbToLinearC(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double linearToSrgbC(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
V3 linearToOklab(double r, double g, double b) {
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
V3 oklabToLinear(double L, double a, double b) {
    const double l_ = L + 0.3963377774 * a + 0.2158037573 * b, m_ = L - 0.1055613458 * a - 0.0638541728 * b, s_ = L - 0.0894841775 * a - 1.2914855480 * b;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}
V3 srgbToOklab(double r, double g, double b) { return linearToOklab(srgbToLinearC(r), srgbToLinearC(g), srgbToLinearC(b)); }
V3 oklabToSrgb(double L, double a, double b) {
    const V3 c = oklabToLinear(L, a, b);
    auto cl = [](double x) { return x < 0 ? 0.0 : x > 1 ? 1.0 : x; };
    return {cl(linearToSrgbC(c[0])), cl(linearToSrgbC(c[1])), cl(linearToSrgbC(c[2]))};
}
double srgb8ToLinear(int k) { return srgbToLinearC(k / 255.0); }

namespace {
using Rgb = std::array<int, 3>;
const std::vector<std::pair<std::string, std::vector<Rgb>>>& palettes() {
    static const std::vector<std::pair<std::string, std::vector<Rgb>>> p = {
        {"gameboy", {{15, 56, 15}, {48, 98, 48}, {139, 172, 15}, {155, 188, 15}}},
        {"cga", {{0, 0, 0}, {0, 255, 255}, {255, 0, 255}, {255, 255, 255}}},
        {"ega", {{0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170}, {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170}, {85, 85, 85}, {85, 85, 255},
                 {85, 255, 85}, {85, 255, 255}, {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}}},
        {"c64", {{0, 0, 0}, {255, 255, 255}, {136, 0, 0}, {170, 255, 238}, {204, 68, 204}, {0, 204, 85}, {0, 0, 170}, {238, 238, 119}, {221, 136, 85},
                 {102, 68, 0}, {255, 119, 119}, {51, 51, 51}, {119, 119, 119}, {170, 255, 102}, {0, 136, 255}, {187, 187, 187}}},
        {"pico8", {{0, 0, 0}, {29, 43, 83}, {126, 37, 83}, {0, 135, 81}, {171, 82, 54}, {95, 87, 79}, {194, 195, 199}, {255, 241, 232}, {255, 0, 77},
                   {255, 163, 0}, {255, 236, 39}, {0, 228, 54}, {41, 173, 255}, {131, 118, 156}, {255, 119, 168}, {255, 204, 170}}},
        {"outrun", {{10, 6, 20}, {26, 12, 48}, {64, 20, 96}, {180, 32, 120}, {255, 78, 160}, {255, 138, 96}, {80, 220, 240}, {244, 236, 255}}},
        {"aurora", {{10, 10, 18}, {16, 40, 44}, {24, 84, 72}, {52, 180, 120}, {120, 230, 180}, {90, 120, 220}, {190, 90, 210}, {245, 240, 255}}},
        {"nes", {{0, 0, 0}, {0, 0, 188}, {0, 120, 248}, {104, 68, 252}, {216, 0, 204}, {248, 56, 0}, {248, 152, 0}, {248, 216, 120}, {0, 168, 0},
                 {0, 168, 136}, {248, 120, 248}, {124, 124, 124}, {188, 188, 188}, {236, 238, 236}}},
        {"zx", {{0, 0, 0}, {0, 0, 215}, {215, 0, 0}, {215, 0, 215}, {0, 215, 0}, {0, 215, 215}, {215, 215, 0}, {215, 215, 215}, {0, 0, 255}, {255, 0, 0},
                {255, 0, 255}, {0, 255, 0}, {0, 255, 255}, {255, 255, 0}, {255, 255, 255}}},
        {"teletext", {{0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0}, {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}}},
        {"vboy", {{0, 0, 0}, {85, 0, 0}, {170, 0, 0}, {255, 0, 0}}},
        {"amber", {{8, 5, 0}, {78, 44, 0}, {178, 108, 12}, {255, 186, 84}}},
        {"mono1", {{8, 8, 12}, {236, 236, 232}}},
    };
    return p;
}
}  // namespace

std::vector<PaletteEntry> labPalette(const std::string& name) {
    for (const auto& [n, rgb] : palettes()) {
        if (n != name) continue;
        std::vector<PaletteEntry> out;
        for (const Rgb& c : rgb) out.push_back({srgbToOklab(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0), c});
        return out;
    }
    throw std::invalid_argument("palette: unknown name " + name);
}
int nearestIndex(double L, double a, double b, const std::vector<PaletteEntry>& pal) {
    int best = 0;
    double bestD = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < pal.size(); ++i) {
        const V3& p = pal[i].lab;
        const double dL = L - p[0], da = a - p[1], db = b - p[2], d = dL * dL + da * da + db * db;
        if (d < bestD) { bestD = d; best = int(i); }
    }
    return best;
}

}  // namespace raw::style
