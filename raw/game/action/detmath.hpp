// raw/game/action/detmath: transcendental functions that give the same bits as Low Water's JS
// combat math (combat/sim/math.mjs, design/DETMATH.md). Only + - * /, sqrt, floor, fmod and
// comparisons, evaluated in the written order; the sources compile with floating-point
// contraction off (no fused multiply-add), so every result is reproducible bit for bit across
// GCC, Clang and MSVC on IEEE 754 doubles. Layer: game.
#pragma once

namespace raw::game::action {

inline constexpr double DPI = 3.141592653589793;   // the double nearest pi (JS Math.PI)

double dsin(double x);            // |x| under about 1.6e6 (the range reduction is exact there)
double dcos(double x);
double datan(double x);
double datan2(double y, double x);  // C's atan2 rules for zeros, signs and infinities
double dasin(double x);           // NaN outside [-1, 1]
double dacos(double x);
double dexp(double x);
double dhypot(double x, double y);  // sqrt(x*x + y*y), no overflow guard

}  // namespace raw::game::action
