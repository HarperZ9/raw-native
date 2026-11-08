// The DETMATH algorithms (design/DETMATH.md in Low Water), transcribed from combat/sim/math.mjs
// line for line. Built with floating-point contraction off (CMakeLists.txt).
#include "raw/game/action/detmath.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace raw::game::action {
namespace {

constexpr double PIO2 = DPI / 2;
constexpr double PIO2_HI = 1.57079632673412561417;
constexpr double PIO2_LO = 6.07710050650619224932e-11;
constexpr double INV_PIO2 = 6.36619772367581382433e-1;
constexpr double PIO6 = DPI / 6;
constexpr double LN2_HI = 6.93147180369123816490e-1;
constexpr double LN2_LO = 1.90821492927058770002e-10;
constexpr double INV_LN2 = 1.44269504088896338700;
constexpr double S1 = -1.66666666666666324348e-1, S2 = 8.33333333332248946124e-3, S3 = -1.98412698298579493134e-4;
constexpr double S4 = 2.75573137070700676789e-6, S5 = -2.50507602534068634195e-8, S6 = 1.58969099521155010221e-10;
constexpr double C1 = 4.16666666666666019037e-2, C2 = -1.38888888888741095749e-3, C3 = 2.48015872894767294178e-5;
constexpr double C4 = -2.75573143513906633035e-7, C5 = 2.08757232129817482790e-9, C6 = -1.13596475577881948265e-11;
const double SQRT3 = std::sqrt(3.0);
const double TAN_PI12 = 2 - SQRT3;
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Inf = std::numeric_limits<double>::infinity();

double ksin(double r) { const double z = r * r; return r + r * z * (S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6))))); }
double kcos(double r) { const double z = r * r; return 1 - 0.5 * z + z * z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6))))); }

// x = k * pi/2 + r; returns r and k mod 4 (a truncating mod, as JS %)
double reduce(double x, int& q) {
    const double k = std::floor(x * INV_PIO2 + 0.5);
    const double r = (x - k * PIO2_HI) - k * PIO2_LO;
    q = (int)std::fmod(std::fmod(k, 4.0) + 4.0, 4.0);
    return r;
}

struct Tables {
    double at[16], ec[14];
    Tables() {
        for (int n = 0; n < 16; n++) at[n] = (n % 2 ? -1.0 : 1.0) / (double)(2 * n + 1);
        ec[0] = 1; for (int n = 1; n <= 13; n++) ec[n] = ec[n - 1] / (double)n;
    }
};
const Tables& tables() { static const Tables t; return t; }

double katan(double t) {
    const double* AT = tables().at;
    const double z = t * t; double p = AT[15];
    for (int n = 14; n >= 0; n--) p = AT[n] + z * p;
    return t * p;
}

// 2^k for -1022 <= k <= 1023, built from its bits
double pow2(int k) { const uint64_t b = (uint64_t)(k + 1023) << 52; double d; std::memcpy(&d, &b, 8); return d; }

}  // namespace

double dsin(double x) {
    if (!std::isfinite(x)) return NaN;
    int q = 0; const double r = reduce(x, q);
    return q == 0 ? ksin(r) : q == 1 ? kcos(r) : q == 2 ? -ksin(r) : -kcos(r);
}

double dcos(double x) {
    if (!std::isfinite(x)) return NaN;
    int q = 0; const double r = reduce(x, q);
    return q == 0 ? kcos(r) : q == 1 ? -ksin(r) : q == 2 ? -kcos(r) : ksin(r);
}

double datan(double x) {
    if (std::isnan(x)) return NaN;
    if (x < 0) return -datan(-x);
    if (x == Inf) return PIO2;
    double base = 0, sign = 1;
    if (x > 1) { x = 1 / x; base = PIO2; sign = -1; }
    const double r = x > TAN_PI12 ? PIO6 + katan((x * SQRT3 - 1) / (x + SQRT3)) : katan(x);
    return base + sign * r;
}

double datan2(double y, double x) {
    if (std::isnan(x) || std::isnan(y)) return NaN;
    const bool yneg = std::signbit(y);
    if (y == 0) {
        if (x > 0 || (x == 0 && !std::signbit(x))) return y;
        return yneg ? -DPI : DPI;
    }
    if (x == 0) return yneg ? -PIO2 : PIO2;
    if (!std::isfinite(x) || !std::isfinite(y)) {
        if (!std::isfinite(y) && !std::isfinite(x)) { const double a = x > 0 ? DPI / 4 : 3 * DPI / 4; return yneg ? -a : a; }
        if (!std::isfinite(y)) return yneg ? -PIO2 : PIO2;
        return x > 0 ? (yneg ? -0.0 : 0.0) : (yneg ? -DPI : DPI);
    }
    const double a = datan(std::fabs(y / x));
    if (x > 0) return yneg ? -a : a;
    return yneg ? -(DPI - a) : DPI - a;
}

double dasin(double x) { return x > 1 || x < -1 ? NaN : datan2(x, std::sqrt((1 - x) * (1 + x))); }
double dacos(double x) { return x > 1 || x < -1 ? NaN : datan2(std::sqrt((1 - x) * (1 + x)), x); }

double dexp(double x) {
    if (std::isnan(x)) return NaN;
    if (x > 709.782712893384) return Inf;
    if (x < -745.1332191019412) return 0;
    const double* EC = tables().ec;
    const double k = std::floor(x * INV_LN2 + 0.5);
    const double r = (x - k * LN2_HI) - k * LN2_LO;
    double p = EC[13];
    for (int n = 12; n >= 0; n--) p = EC[n] + r * p;
    const int ki = (int)k;
    if (ki > 1023) return p * pow2(1023) * pow2(ki - 1023);
    if (ki < -1000) return p * pow2(ki + 1000) * pow2(-1000);
    return p * pow2(ki);
}

double dhypot(double x, double y) { return std::sqrt(x * x + y * y); }

}  // namespace raw::game::action
