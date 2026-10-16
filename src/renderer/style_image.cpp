// Shared arithmetic of the image style passes: see raw/renderer/style_image.hpp.
#include "raw/renderer/style_image.hpp"
#include "data/pigments.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
namespace raw::style {

std::uint32_t pcg(std::uint32_t v) {
    const std::uint32_t state = v * 747796405u + 2891336453u;
    const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
double hash3(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return double(pcg(x ^ pcg(y ^ pcg(z)))) / 4294967296.0; }
double fade5(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
double linearToSrgb(double l) { l = std::clamp(l, 0.0, 1.0); return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055; }

namespace {
double srgbOf(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
double displayCodes(double v) { return srgbOf(std::min(1.0, std::max(0.0, v))) * 255; }
double labCodes(double v, double gain) { return srgbOf(std::min(1.0, 1 - std::exp(-std::max(0.0, v) * gain))) * 255; }
double edge(double x) { return std::abs(x - std::floor(x) - 0.5); }
}  // namespace
// Math.round of a non-negative value is floor(x + 0.5).
std::uint8_t encodeDisplay8(double v) { return std::uint8_t(std::floor(displayCodes(v) + 0.5)); }
std::uint8_t encodeLab8(double v, double gain) { return std::uint8_t(std::floor(labCodes(v, gain) + 0.5)); }
double edgeDisplay8(double v) { return edge(displayCodes(v)); }
double edgeLab8(double v, double gain) { return edge(labCodes(v, gain)); }

double bodyReflectance(const V4& c, int w) {
    double K = 0, S = 0;
    for (std::size_t i = 0; i < 4; ++i) { K += c[i] * data::kPigmentK[w][i]; S += c[i] * data::kPigmentS[w][i]; }
    const double q = K / std::max(S, 1e-9);
    return 1 + q - std::sqrt(q * q + 2 * q);
}
V3 kmDecode(const V4& c) {
    V3 out{0, 0, 0};
    for (int w = 0; w < kWavelengths; ++w) {
        const double R = bodyReflectance(c, w);
        for (std::size_t k = 0; k < 3; ++k) out[k] += data::kPigmentW[w][k] * R;
    }
    return out;
}
V4 lutConc(const V3& s) {
    const int N = kLutN;
    double f[3], t[3]; int i0[3];
    for (int k = 0; k < 3; ++k) {
        f[k] = std::min(1.0, std::max(0.0, s[std::size_t(k)])) * (N - 1);
        i0[k] = std::min(N - 2, int(std::floor(f[k])));
        t[k] = f[k] - i0[k];
    }
    V4 c{0, 0, 0, 0};
    for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const double w = (dx ? t[0] : 1 - t[0]) * (dy ? t[1] : 1 - t[1]) * (dz ? t[2] : 1 - t[2]);
        const std::size_t o = std::size_t((((i0[2] + dz) * N + i0[1] + dy) * N + i0[0] + dx) * 4);
        for (std::size_t k = 0; k < 4; ++k) c[k] += w * double(data::kLatentLut[o + k]);
    }
    return c;
}
Latent latentOf(const V3& rgb) {
    Latent L;
    L.c = lutConc({linearToSrgb(std::min(1.0, std::max(0.0, rgb[0]))), linearToSrgb(std::min(1.0, std::max(0.0, rgb[1]))),
                   linearToSrgb(std::min(1.0, std::max(0.0, rgb[2])))});
    const V3 d = kmDecode(L.c);
    L.r = {rgb[0] - d[0], rgb[1] - d[1], rgb[2] - d[2]};
    return L;
}
double tableW(int w, int k) { return data::kPigmentW[w][k]; }
std::vector<float> packPigments() {
    std::vector<float> v;
    for (const auto& r : data::kPigmentK) for (double x : r) v.push_back(float(x));
    for (const auto& r : data::kPigmentS) for (double x : r) v.push_back(float(x));
    for (const auto& r : data::kPigmentW) for (double x : r) v.push_back(float(x));
    v.insert(v.end(), std::begin(data::kLatentLut), std::end(data::kLatentLut));
    return v;
}

}  // namespace raw::style
