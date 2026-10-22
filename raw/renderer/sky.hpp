#pragma once
// Analytic skies and the engine's own baked environment maps (evidence/sky-envmaps-bounds.json).
// Directions are world space with +Y up; radiance is linear Rec.709 in nits.
//   Preetham, Shirley and Smits, "A Practical Analytic Model for Daylight", SIGGRAPH 1999:
//     the Perez distribution with turbidity-linear coefficients, zenith luminance and chromaticity.
//   CIE overcast sky: L = Lz (1 + 2 cos theta) / 3. CIE standard clear sky (ISO 15469 type 12)
//     is kept as the independent cross-check of the clear model.
// The sun and moon are disks too small for a cube texel; a bake deposits each into the texel that
// holds its direction with the disk's illuminance (energy kept, shape not).
#include "raw/renderer/lighting.hpp"
#include <cstdint>
#include <string>
namespace raw::sky {

using lighting::D3;
using lighting::Rgb;

struct Clear { double turbidity{2.5}; D3 sun{0, 1, 0}; double groundAlbedo{0.25}; };   // sun: unit, towards the sun
Rgb preetham(const Clear& c, D3 dir);                  // sky only (no disk); below the horizon, the ground
double preethamZenithNits(const Clear& c);
double cieClearRelative(D3 sun, D3 dir);               // ISO 15469 type 12, luminance relative to the zenith
Rgb overcast(double zenithNits, D3 dir, double groundAlbedo = 0.25);
// The sun's illuminance (lux, per channel) after a simple extinction exp(-tau m), tau = (0.08 +
// 0.025 T) x (0.6, 1.0, 1.8) for (R, G, B), m = 1 / max(cos zenith, 0.05). Stated, not fitted.
Rgb sunIlluminance(const Clear& c);

// The environments of the lockfile (evidence/envmaps.lock.json).
enum class Env : std::uint8_t { ClearNoon, Overcast, GoldenHour, Night };
const char* name(Env e);
// Level 0 of a cube (faces of `size` texels) with `levels` levels allocated (the rest zero, for a
// prefilter); world space, so a view-space caller rotates directions itself.
lighting::Cube bake(Env e, int size = 64, int levels = 6, D3 (*viewToWorld)(D3) = nullptr);   // viewToWorld: bake for a rotated camera
// The texel solid angle at a texel centre (face coordinates in [0, 1]).
double texelSolidAngle(int size, double u, double v);

}  // namespace raw::sky
