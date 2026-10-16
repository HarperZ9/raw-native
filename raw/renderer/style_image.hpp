#pragma once
// Shared arithmetic of the image style passes in the C++ core (ADR 0019): the shader library's
// hash, value-noise fade, sRGB transfer, the two 8-bit display encodes the web parity pages
// compare, and the Kubelka-Munk pigment mixing of web/shaders/paint/pigments.mjs. Every function
// does the operations of its JavaScript twin (web/shaders/common.mjs, purity.mjs, lab/gpu-kit.mjs,
// paint/pigments.mjs) in the same order, in float64. The pigment tables are generated from the
// JavaScript into src/renderer/data/pigments.hpp (tools/port/gen_pigments.mjs, ADR 0019 rule 4).
#include "raw/renderer/style_ditherlight.hpp"   // V3
#include <array>
#include <cstdint>
#include <vector>
namespace raw::style {

using V4 = std::array<double, 4>;

std::uint32_t pcg(std::uint32_t v);
double hash3(std::uint32_t x, std::uint32_t y, std::uint32_t z);   // uniform in [0, 1)
double fade5(double t);
double linearToSrgb(double l);                                     // clamps to [0, 1] first

// displayEncode8 (purity.mjs): clamp, sRGB, round half up to a code.
std::uint8_t encodeDisplay8(double v);
// labEncode8 (lab/gpu-kit.mjs): the soft shoulder 1 - exp(-gain v), sRGB, round half up.
std::uint8_t encodeLab8(double v, double gain = 2.4);
// How far the encode's value, in codes, lies from its rounding edge (x.5): a tie when tiny.
double edgeDisplay8(double v);
double edgeLab8(double v, double gain = 2.4);

// Kubelka-Munk: 31 wavelengths (400 to 700 nm), four pigments.
inline constexpr int kWavelengths = 31;
inline constexpr int kLutN = 17;
V3 kmDecode(const V4& c);                       // concentrations to linear RGB
V4 lutConc(const V3& srgb);                     // trilinear latent lookup of an sRGB-encoded colour
struct Latent { V4 c; V3 r; };
Latent latentOf(const V3& rgb);                 // a linear colour as concentrations plus residual
double bodyReflectance(const V4& c, int w);     // R at wavelength index w of concentrations c
// The tables the GPU reads, packed as web/shaders/paint/gpu.mjs packPigments: K (31 x 4), S
// (31 x 4), W (31 x 3), then the 17^3 x 4 lookup, as f32.
std::vector<float> packPigments();
double tableW(int w, int k);                    // W_TAB[w][k]

}  // namespace raw::style
