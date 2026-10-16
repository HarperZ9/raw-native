#pragma once
// Curvature hatching from the SDF Hessian, the C++ core's float64 reference (ADR 0019). The GPU
// pass is style_hatch in src/renderer/gpu/shaders/style.wgsl; the JavaScript reference it is
// checked against is web/shaders/hatch/hatch.mjs, whose header carries the model and prior work.
//
// The pass ray-marches a signed distance field, takes the Hessian of the field at the hit by central
// differences (step 0.01), restricts it to the tangent plane and divides by the gradient's length:
// the shape operator, whose eigenvectors are the principal directions. Lines run along the strongest
// curvature; at umbilics (spheres, planes) the direction is undefined and the lines become planar
// slices. Line spacing is in world units with octave crossfades, width in screen pixels.
// The field is the owned still life of web/shaders/fixtures/stilllife.mjs (ground, sphere, torus,
// capsule, rounded box) seen from its fixed camera; another scene would supply its own field.
#include "raw/renderer/style_ditherlight.hpp"   // V3
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct HatchPlan {
    double freq{34}, width{0.55};
    int layers{1};
    double umb{0.12}, contour{0};
    V3 ink{0.12, 0.1, 0.11}, paper{0.95, 0.93, 0.88};
    double toneFloor{0.12};   // the 0.12 in 0.12 + 0.88 max(0, n.L); a source-edit control changes it
};
HatchPlan hatchPreset(const std::string& name);   // "engraving", "crosshatch" or "ink"

double stillSdf(const V3& p);
struct Curvature { V3 n, dir, other; bool umbilic{false}; };
Curvature curvature(const V3& p, double umb);

struct HatchFrame {
    std::vector<double> rgba;                      // display-linear, alpha 1
    std::vector<std::uint8_t> hit, fallback;       // the ray hit the field; the umbilic fallback was used
};
HatchFrame runHatch(const HatchPlan& p, int w, int h);

}  // namespace raw::style
