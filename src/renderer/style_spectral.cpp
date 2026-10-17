// Spectral colour: see raw/renderer/style_spectral.hpp.
#include "raw/renderer/style_spectral.hpp"
#include "data/cie1931.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style::spectral {

T3 cmf(double wl) {
    const double f = (wl - data::kCmfStart) / data::kCmfStep;
    if (f < 0 || f > data::kCmfN - 1) return {0, 0, 0};
    const int i = std::min(data::kCmfN - 2, int(std::floor(f)));
    const double t = f - i;
    return {data::kCmfX[i] + (data::kCmfX[i + 1] - data::kCmfX[i]) * t, data::kCmfY[i] + (data::kCmfY[i + 1] - data::kCmfY[i]) * t,
            data::kCmfZ[i] + (data::kCmfZ[i + 1] - data::kCmfZ[i]) * t};
}
M3 mul3(const M3& a, const M3& b) {
    M3 o{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) o[std::size_t(r * 3 + c)] = a[std::size_t(r * 3)] * b[std::size_t(c)] + a[std::size_t(r * 3 + 1)] * b[std::size_t(3 + c)] + a[std::size_t(r * 3 + 2)] * b[std::size_t(6 + c)];
    return o;
}
T3 apply3(const M3& m, const T3& v) { return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]}; }
M3 inv3(const M3& m) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], gg = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * gg), C = d * h - e * gg, det = a * A + b * B + c * C;
    return {A / det, -(b * i - c * h) / det, (b * f - c * e) / det, B / det, (a * i - c * gg) / det, -(a * f - c * d) / det, C / det, -(a * h - b * gg) / det, (a * e - b * d) / det};
}
T3 xyToXYZ(double x, double y, double Y) { return {(x * Y) / y, Y, ((1 - x - y) * Y) / y}; }
M3 rgbToXYZ709() {
    const T3 P[3] = {xyToXYZ(0.64, 0.33), xyToXYZ(0.30, 0.60), xyToXYZ(0.15, 0.06)};
    const M3 M = {P[0][0], P[1][0], P[2][0], P[0][1], P[1][1], P[2][1], P[0][2], P[1][2], P[2][2]};
    const T3 s = apply3(inv3(M), xyToXYZ(0.3127, 0.329));
    return {M[0] * s[0], M[1] * s[1], M[2] * s[2], M[3] * s[0], M[4] * s[1], M[5] * s[2], M[6] * s[0], M[7] * s[1], M[8] * s[2]};
}
M3 xyzToRec709() { return inv3(rgbToXYZ709()); }

}  // namespace raw::style::spectral
