#pragma once
// OKLab and the retro palettes of the shader library, the C++ core's float64 forms (ADR 0019). The
// JavaScript twins are web/shaders/reference/contracts.mjs (the four colour functions, vendored
// from the author's site) and retro-palettes.mjs (the hardware palettes and nearestIndex); the
// operations are the same, in the same order.
#include "raw/renderer/style_ditherlight.hpp"   // V3
#include <array>
#include <string>
#include <vector>
namespace raw::style {

double srgbToLinearC(double c);    // contracts.mjs srgbToLinear (no clamp)
double linearToSrgbC(double c);    // contracts.mjs linearToSrgb (no clamp)
V3 linearToOklab(double r, double g, double b);
V3 oklabToLinear(double L, double a, double b);
V3 srgbToOklab(double r, double g, double b);   // sRGB-encoded 0..1
V3 oklabToSrgb(double L, double a, double b);   // sRGB-encoded, each clamped to 0..1

struct PaletteEntry { V3 lab; std::array<int, 3> rgb; };
// The named hardware palettes of retro-palettes.mjs (gameboy, cga, ega, c64, pico8, outrun,
// aurora, nes, zx, teletext, vboy, amber, mono1). Throws std::invalid_argument on another name.
std::vector<PaletteEntry> labPalette(const std::string& name);
int nearestIndex(double L, double a, double b, const std::vector<PaletteEntry>& pal);
// sRGB byte k to linear light, srgbToLinearC(k / 255), as retro-palettes.mjs SRGB8_TO_LINEAR.
double srgb8ToLinear(int k);

}  // namespace raw::style
