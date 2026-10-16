// Film stocks and the per-pixel spectral path: see raw/renderer/style_film.hpp. Operations are in
// the order of web/shaders/film/stocks.mjs and web/shaders/spectral.mjs.
#include "raw/renderer/style_film.hpp"
#include "data/cie1931.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
constexpr int kNL = 41;
using Spec = std::function<double(double)>;
double lambdaAt(int w) { return 380 + 10 * w; }
Spec g(double c, double fwhm) { const double s = fwhm / 2.3548; return [c, s](double l) { const double q = (l - c) / s; return std::exp(-0.5 * (q * q)); }; }

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
T3 xyToXYZ(double x, double y, double Y = 1) { return {(x * Y) / y, Y, ((1 - x - y) * Y) / y}; }
M3 rgbToXYZ709() {
    const T3 P[3] = {xyToXYZ(0.64, 0.33), xyToXYZ(0.30, 0.60), xyToXYZ(0.15, 0.06)};
    const M3 M = {P[0][0], P[1][0], P[2][0], P[0][1], P[1][1], P[2][1], P[0][2], P[1][2], P[2][2]};
    const T3 s = apply3(inv3(M), xyToXYZ(0.3127, 0.329));
    return {M[0] * s[0], M[1] * s[1], M[2] * s[2], M[3] * s[0], M[4] * s[1], M[5] * s[2], M[6] * s[0], M[7] * s[1], M[8] * s[2]};
}
Spec blackbody(double T) {
    return [T](double wl) { const double c2 = 1.4387769e-2, l = wl * 1e-9; return 1 / (std::pow(l, 5) * (std::exp(c2 / (l * T)) - 1)); };
}
double curve(double x, double dmin, double span, double k, double x0) { return dmin + span / (1 + std::exp(-k * (x - x0))); }
double curveInv(double D, double dmin, double span, double k, double x0) { return x0 - std::log(span / (D - dmin) - 1) / k; }

struct Negative { Spec sens[3], prim[3], sec[3]; double gamma, span, dmin, base, interimage, sigmaD48; T3 grainUm; };
Negative negativeOf(const std::string& name) {
    Negative N;
    N.sens[0] = g(645, 55); N.sens[1] = g(545, 55); N.sens[2] = g(445, 60);
    N.prim[0] = g(670, 100); N.prim[1] = g(550, 85); N.prim[2] = g(450, 90);
    const Spec a = g(550, 80), b = g(440, 60), c = g(435, 60);
    N.sec[0] = [a, b](double l) { return 0.3 * a(l) + 0.08 * b(l); };
    N.sec[1] = [c](double l) { return 0.15 * c(l); };
    N.sec[2] = [](double) { return 0.0; };
    N.gamma = 0.62; N.span = 2.6; N.dmin = 0.05; N.base = 0.1; N.interimage = 0.3;
    if (name == "neg-500t") { N.grainUm = {3.2, 3.4, 4.2}; N.sigmaD48 = 0.008; return N; }
    if (name == "neg-250d") { N.grainUm = {2.4, 2.5, 3.0}; N.sigmaD48 = 0.006; return N; }
    throw std::invalid_argument("film: unknown negative " + name);
}

std::vector<T3> rgbBasis() {
    const Spec B[3] = {[](double l) { return 1 / (1 + std::exp(-(l - 595) / 14)); }, g(540, 90), g(450, 70)};
    M3 P{};
    double ynorm = 0;
    for (int w = 0; w < kNL; ++w) ynorm += cmf(lambdaAt(w))[1];
    for (int w = 0; w < kNL; ++w) {
        const double l = lambdaAt(w);
        const T3 c = cmf(l);
        for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) P[std::size_t(r * 3 + k)] += (c[std::size_t(r)] * B[k](l)) / ynorm;
    }
    const M3 A = mul3(inv3(P), rgbToXYZ709());
    std::vector<T3> out(kNL);
    for (int w = 0; w < kNL; ++w) {
        const double l = lambdaAt(w);
        for (int c = 0; c < 3; ++c) out[std::size_t(w)][std::size_t(c)] = B[0](l) * A[std::size_t(c)] + B[1](l) * A[std::size_t(3 + c)] + B[2](l) * A[std::size_t(6 + c)];
    }
    return out;
}

T3 printExposure(const FilmStock& t, const T3& D) {
    T3 out{0, 0, 0};
    for (int w = 0; w < kNL; ++w) {
        const T3& p = t.negPrim[std::size_t(w)];
        const double T = std::pow(10, -(D[0] * p[0] + D[1] * p[1] + D[2] * p[2] + t.negConst[std::size_t(w)]));
        for (int j = 0; j < 3; ++j) out[std::size_t(j)] += t.printW[std::size_t(w)][std::size_t(j)] * T;
    }
    return out;
}
T3 negDensities(const FilmStock& t, const T3& rgb) {
    const T3 H0 = apply3(t.E, rgb);
    T3 D{};
    for (int i = 0; i < 3; ++i) D[std::size_t(i)] = curve(std::log10(std::max(H0[std::size_t(i)] * 1.0, 1e-6)), t.negDmin[std::size_t(i)], t.negSpan[std::size_t(i)], t.negK[std::size_t(i)], t.negX0[std::size_t(i)]);
    return filmInterimage(t, D);
}
M3 adaptToD65(const FilmStock& t) {
    T3 white{0, 0, 0};
    for (int w = 0; w < kNL; ++w) {
        double a = 0;
        for (int j = 0; j < 3; ++j) a = a + t.prtDmin[std::size_t(j)] * t.printPrim[std::size_t(w)][std::size_t(j)];
        const double T = std::pow(10, -(t.printBase + a));
        for (int c = 0; c < 3; ++c) white[std::size_t(c)] += t.projV[std::size_t(w)][std::size_t(c)] * T;
    }
    const M3 BR = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    const T3 s = apply3(BR, white), d = apply3(BR, xyToXYZ(0.3127, 0.329, white[1]));
    const M3 cat = mul3(inv3(BR), mul3({d[0] / s[0], 0, 0, 0, d[1] / s[1], 0, 0, 0, d[2] / s[2]}, BR));
    M3 o = mul3(inv3(rgbToXYZ709()), cat);
    for (double& v : o) v = v / white[1];
    return o;
}
void timePrint(FilmStock& t) {
    const double GREY = 0.18;
    const T3 grey{GREY, GREY, GREY};
    auto resid = [&](const T3& lg) { FilmStock s = t; s.logGain = lg; const T3 o = filmPrintPixel(s, negDensities(s, grey)); return T3{o[0] - GREY, o[1] - GREY, o[2] - GREY}; };
    T3 lg{0, 0, 0};
    for (int j = 0; j < 3; ++j) {
        const T3 D = negDensities(t, grey);
        const double H = printExposure(t, D)[std::size_t(j)];
        lg[std::size_t(j)] = curveInv(t.prtDmin[std::size_t(j)] + 0.8, t.prtDmin[std::size_t(j)], t.prtSpan[std::size_t(j)], t.prtK[std::size_t(j)], 0) - std::log10(H);
    }
    for (int it = 0; it < 30; ++it) {
        const T3 r = resid(lg);
        if (std::hypot(r[0], r[1], r[2]) < 1e-7) break;
        M3 J{};
        for (int c = 0; c < 3; ++c) {
            T3 d = lg; d[std::size_t(c)] += 1e-4;
            const T3 rd = resid(d);
            for (int r2 = 0; r2 < 3; ++r2) J[std::size_t(r2 * 3 + c)] = (rd[std::size_t(r2)] - r[std::size_t(r2)]) / 1e-4;
        }
        const T3 step = apply3(inv3(J), r);
        for (int c = 0; c < 3; ++c) lg[std::size_t(c)] = lg[std::size_t(c)] - step[std::size_t(c)];
    }
    t.logGain = lg;
}
}  // namespace

T3 filmInterimage(const FilmStock& t, const T3& D) {
    T3 d{};
    for (int i = 0; i < 3; ++i) d[std::size_t(i)] = D[std::size_t(i)] - t.negDmin[std::size_t(i)];
    const double m = (d[0] + d[1] + d[2]) / 3;
    T3 o{};
    for (int i = 0; i < 3; ++i) o[std::size_t(i)] = t.negDmin[std::size_t(i)] + d[std::size_t(i)] + t.interimage * (d[std::size_t(i)] - m);
    return o;
}
T3 filmPrintPixel(const FilmStock& t, const T3& D) {
    const T3 H = printExposure(t, D);
    T3 Dp{};
    for (int j = 0; j < 3; ++j) Dp[std::size_t(j)] = curve(std::log10(std::max(H[std::size_t(j)], 1e-12)) + t.logGain[std::size_t(j)], t.prtDmin[std::size_t(j)], t.prtSpan[std::size_t(j)], t.prtK[std::size_t(j)], 0);
    const double silver = t.bleach * (Dp[0] + Dp[1] + Dp[2] - t.prtDmin[0] - t.prtDmin[1] - t.prtDmin[2]) / 3;
    T3 xyz{0, 0, 0};
    for (int w = 0; w < kNL; ++w) {
        const T3& p = t.printPrim[std::size_t(w)];
        const double T = std::pow(10, -(Dp[0] * p[0] + Dp[1] * p[1] + Dp[2] * p[2] + t.printBase + silver));
        for (int c = 0; c < 3; ++c) xyz[std::size_t(c)] += t.projV[std::size_t(w)][std::size_t(c)] * T;
    }
    return apply3(t.out, xyz);
}

FilmStock buildFilmStock(const FilmParams& p) {
    const Negative N = negativeOf(p.negative);
    if (p.print != "print-2383") throw std::invalid_argument("film: unknown print " + p.print);
    const Spec psens[3] = {g(690, 40), g(545, 45), g(445, 50)};
    const Spec d0a = g(655, 100), d0b = g(560, 80), d0c = g(440, 60), d1a = g(555, 80), d1b = g(435, 60);
    const Spec pdyes[3] = {[=](double l) { return d0a(l) + 0.22 * d0b(l) + 0.06 * d0c(l); }, [=](double l) { return d1a(l) + 0.1 * d1b(l); }, g(445, 90)};
    const double prGamma = 2.8, prSpan = 3.9, prDmin = 0.06, prBase = 0.05;
    FilmStock t;
    const std::vector<T3> basis = rgbBasis();
    for (int w = 0; w < kNL; ++w)
        for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c) t.E[std::size_t(i * 3 + c)] += N.sens[i](lambdaAt(w)) * basis[std::size_t(w)][std::size_t(c)];
    for (int i = 0; i < 3; ++i) { const double s = t.E[std::size_t(i * 3)] + t.E[std::size_t(i * 3 + 1)] + t.E[std::size_t(i * 3 + 2)]; for (int c = 0; c < 3; ++c) t.E[std::size_t(i * 3 + c)] /= s; }
    const Spec lamp = blackbody(3200), xenon = blackbody(5800);
    T3 wsum{0, 0, 0};
    for (int w = 0; w < kNL; ++w) {
        const double l = lambdaAt(w);
        t.negPrim.push_back({N.prim[0](l), N.prim[1](l), N.prim[2](l)});
        double r = 0;
        for (int i = 0; i < 3; ++i) r = r + (N.dmin + N.span) * N.sec[i](l);
        t.negConst.push_back(N.base + r);
        t.printW.push_back({lamp(l) * psens[0](l), lamp(l) * psens[1](l), lamp(l) * psens[2](l)});
        t.printPrim.push_back({pdyes[0](l), pdyes[1](l), pdyes[2](l)});
        const T3 c = cmf(l);
        t.projV.push_back({c[0] * xenon(l), c[1] * xenon(l), c[2] * xenon(l)});
    }
    for (int j = 0; j < 3; ++j) for (int w = 0; w < kNL; ++w) wsum[std::size_t(j)] = wsum[std::size_t(j)] + t.printW[std::size_t(w)][std::size_t(j)];
    for (auto& r : t.printW) for (int j = 0; j < 3; ++j) r[std::size_t(j)] /= wsum[std::size_t(j)];
    double openY = 0;
    const double dsum = ((0 + prDmin) + prDmin) + prDmin;
    for (int w = 0; w < kNL; ++w) openY = openY + t.projV[std::size_t(w)][1] * std::pow(10, -(prBase + dsum / 3));
    for (auto& r : t.projV) for (double& v : r) v = v / openY;
    for (int i = 0; i < 3; ++i) {
        t.negDmin[std::size_t(i)] = N.dmin; t.negSpan[std::size_t(i)] = N.span; t.negK[std::size_t(i)] = (4 * N.gamma) / N.span; t.negX0[std::size_t(i)] = std::log10(0.18) + 0.6;
        t.prtDmin[std::size_t(i)] = prDmin; t.prtSpan[std::size_t(i)] = prSpan; t.prtK[std::size_t(i)] = (4 * prGamma) / prSpan;
    }
    t.printBase = prBase;
    if (p.process == "normal") t.bleach = 0;
    else if (p.process == "bleach-bypass") t.bleach = 0.7;
    else throw std::invalid_argument("film: unknown process " + p.process);
    t.interimage = N.interimage; t.grainUm = N.grainUm; t.sigmaD48 = N.sigmaD48;
    t.out = adaptToD65(t);
    timePrint(t);
    for (int j = 0; j < 3; ++j) t.logGain[std::size_t(j)] = t.logGain[std::size_t(j)] + 0.025 * p.printerPoints[std::size_t(j)];
    return t;
}

FilmParams filmPreset(const std::string& name) {
    FilmParams p;
    if (name == "500t-print") return p;
    if (name == "500t-no-remjet") { p.remjet = false; return p; }
    if (name == "250d-print") { p.negative = "neg-250d"; return p; }
    if (name == "bleach-bypass") { p.process = "bleach-bypass"; return p; }
    throw std::invalid_argument("film: unknown preset " + name);
}

}  // namespace raw::style
