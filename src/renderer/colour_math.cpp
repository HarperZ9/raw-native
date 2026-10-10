// Colour primaries, matrices, transfer functions and the PBR Neutral and AgX tone
// mappers. Double precision throughout: this is the CPU reference.
#include "raw/renderer/colour.hpp"
#include <algorithm>
#include <cmath>
namespace raw::colour {

const Primaries kRec709  = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}, {0.3127, 0.3290}};
const Primaries kP3D65   = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.3127, 0.3290}};
const Primaries kRec2020 = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, {0.3127, 0.3290}};
const Primaries kAP0     = {{0.7347, 0.2653}, {0.0000, 1.0000}, {0.0001, -0.0770}, {0.32168, 0.33767}};
const Primaries kAP1     = {{0.713, 0.293}, {0.165, 0.830}, {0.128, 0.044}, {0.32168, 0.33767}};

Mat3 multiply(const Mat3& a, const Mat3& b){
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = a[i * 3 + 0] * b[0 * 3 + j] + a[i * 3 + 1] * b[1 * 3 + j] + a[i * 3 + 2] * b[2 * 3 + j];
    return r;
}

Mat3 inverse(const Mat3& m){
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], k = m[8];
    const double A = e * k - f * h, B = -(d * k - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    return {A / det, -(b * k - c * h) / det, (b * f - c * e) / det,
            B / det, (a * k - c * g) / det, -(a * f - c * d) / det,
            C / det, -(a * h - b * g) / det, (a * e - b * d) / det};
}

// The normalised primary matrix, as OCIO's rgb2xyz_from_xy builds it.
Mat3 rgbToXyz(const Primaries& p){
    const Mat3 xyz = {p.r[0], p.g[0], p.b[0], p.r[1], p.g[1], p.b[1],
                      1. - p.r[0] - p.r[1], 1. - p.g[0] - p.g[1], 1. - p.b[0] - p.b[1]};
    const Mat3 inv = inverse(xyz);
    const double w[3] = {p.w[0] / p.w[1], 1.0, (1.0 - p.w[0] - p.w[1]) / p.w[1]};
    Mat3 r{};
    for (int i = 0; i < 3; ++i){
        const double gain = w[0] * inv[i * 3 + 0] + w[1] * inv[i * 3 + 1] + w[2] * inv[i * 3 + 2];
        for (int j = 0; j < 3; ++j) r[j * 3 + i] = gain * xyz[j * 3 + i];
    }
    return r;
}

static Mat3 bradford(const double srcXYZ[3], const double dstXYZ[3]){
    const Mat3 M = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    double s[3], d[3];
    for (int i = 0; i < 3; ++i){
        s[i] = M[i * 3] * srcXYZ[0] + M[i * 3 + 1] * srcXYZ[1] + M[i * 3 + 2] * srcXYZ[2];
        d[i] = M[i * 3] * dstXYZ[0] + M[i * 3 + 1] * dstXYZ[1] + M[i * 3 + 2] * dstXYZ[2];
    }
    const Mat3 scale = {d[0] / s[0], 0, 0, 0, d[1] / s[1], 0, 0, 0, d[2] / s[2]};
    return multiply(inverse(M), multiply(scale, M));
}

Mat3 conversion(const Primaries& src, const Primaries& dst, bool adapt){
    const Mat3 s2x = rgbToXyz(src), d2x = rgbToXyz(dst);
    const Mat3 x2d = inverse(d2x);
    if (!adapt || (src.w[0] == dst.w[0] && src.w[1] == dst.w[1])) return multiply(x2d, s2x);
    const double sw[3] = {s2x[0] + s2x[1] + s2x[2], s2x[3] + s2x[4] + s2x[5], s2x[6] + s2x[7] + s2x[8]};
    const double dw[3] = {d2x[0] + d2x[1] + d2x[2], d2x[3] + d2x[4] + d2x[5], d2x[6] + d2x[7] + d2x[8]};
    return multiply(x2d, multiply(bradford(sw, dw), s2x));
}

RGB apply(const Mat3& m, const RGB& v){
    return {(float)(m[0] * v[0] + m[1] * v[1] + m[2] * v[2]), (float)(m[3] * v[0] + m[4] * v[1] + m[5] * v[2]),
            (float)(m[6] * v[0] + m[7] * v[1] + m[8] * v[2])};
}

double srgbEncode(double x){
    x = std::max(0.0, x);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}
double srgbDecode(double v){ return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double bt1886Encode(double x){ return std::pow(std::max(0.0, x), 1.0 / 2.4); }
double bt1886Decode(double v){ return std::pow(std::max(0.0, v), 2.4); }

namespace {
constexpr double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
constexpr double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
}
double pqEncode(double linear100){
    const double y = std::max(0.0, linear100) / 100.0;       // 1.0 = 10,000 nits
    const double p = std::pow(y, m1);
    return std::pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}
double pqDecode(double v){
    const double p = std::pow(std::max(0.0, v), 1.0 / m2);
    return 100.0 * std::pow(std::max(0.0, p - c1) / (c2 - c3 * p), 1.0 / m1);
}

RGB pbrNeutral(const RGB& in){
    const double start = 0.8 - 0.04, desat = 0.15;
    double c[3] = {in[0], in[1], in[2]};
    const double x = std::min({c[0], c[1], c[2]});
    const double offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    for (double& v : c) v -= offset;
    const double peak = std::max({c[0], c[1], c[2]});
    if (peak < start) return {(float)c[0], (float)c[1], (float)c[2]};
    const double d = 1.0 - start;
    const double newPeak = 1.0 - d * d / (peak + d - start);
    const double g = 1.0 - 1.0 / (desat * (peak - newPeak) + 1.0);
    RGB out;
    for (int i = 0; i < 3; ++i) out[i] = (float)((c[i] * newPeak / peak) * (1.0 - g) + newPeak * g);
    return out;
}

RGB agx(const RGB& in){
    // Filament's AgX (Blender's, with Rec.2020 working primaries). The GLSL
    // matrices are column-major; these are the same matrices, row-major.
    static const Mat3 inset = {0.856627153315983, 0.0951212405381588, 0.0482516061458583,
                               0.137318972929847, 0.761241990602591, 0.101439036467562,
                               0.11189821299995, 0.0767994186031903, 0.811302368396859};
    static const Mat3 outset = {1.1271005818144368, -0.11060664309660323, -0.016493938717834573,
                                -0.1413297634984383, 1.157823702216272, -0.016493938717834257,
                                -0.14132976349843826, -0.11060664309660294, 1.2519364065950405};
    static const Mat3 to2020 = conversion(kRec709, kRec2020), from2020 = conversion(kRec2020, kRec709);
    const double minEv = -12.47393, maxEv = 4.026069;
    const Mat3 pre = multiply(inset, to2020);
    double v[3];
    for (int i = 0; i < 3; ++i){
        double x = pre[i * 3] * in[0] + pre[i * 3 + 1] * in[1] + pre[i * 3 + 2] * in[2];
        x = (std::log2(std::max(x, 1e-10)) - minEv) / (maxEv - minEv);
        x = std::clamp(x, 0.0, 1.0);
        const double x2 = x * x, x4 = x2 * x2;
        v[i] = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
    }
    double o[3];
    for (int i = 0; i < 3; ++i) o[i] = std::pow(std::max(0.0, outset[i * 3] * v[0] + outset[i * 3 + 1] * v[1] + outset[i * 3 + 2] * v[2]), 2.2);
    RGB out;
    for (int i = 0; i < 3; ++i)
        out[i] = (float)std::clamp(from2020[i * 3] * o[0] + from2020[i * 3 + 1] * o[1] + from2020[i * 3 + 2] * o[2], 0.0, 1.0);
    return out;
}

}
