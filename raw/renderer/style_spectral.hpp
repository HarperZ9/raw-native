#pragma once
// Spectral colour for the C++ core's style references (ADR 0019): the CIE 1931 colour matching
// functions by linear interpolation, 3 x 3 matrices as flat row-major arrays, and the Rec.709
// primaries with the D65 white. The operations of web/shaders/spectral.mjs, in its order; the
// CMF table is generated from web/shaders/cie1931.mjs (tools/port/gen_cie.mjs).
#include <array>
namespace raw::style::spectral {

using M3 = std::array<double, 9>;
using T3 = std::array<double, 3>;
T3 cmf(double wl);                       // zero outside 380 to 780 nm
M3 mul3(const M3& a, const M3& b);
T3 apply3(const M3& m, const T3& v);
M3 inv3(const M3& m);
T3 xyToXYZ(double x, double y, double Y = 1);
M3 rgbToXYZ709();                        // Rec.709 primaries, D65 white at Y = 1
M3 xyzToRec709();                        // its inverse (spectral.mjs XYZ_TO.rec709)

}  // namespace raw::style::spectral
