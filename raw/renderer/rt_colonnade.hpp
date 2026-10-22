#pragma once
// The sunlit colonnade (ROADMAP P11; evidence/colonnade-bounds.json): a stone floor, a terracotta
// wall, six marble columns under five semicircular arches and an entablature, a near-mirror sphere,
// a rough copper sphere and a small warm emissive sphere, lit by the Preetham sky (raw/renderer/
// sky.hpp) as the path tracer's environment and the sun as a directional light. Our own geometry.
#include "raw/renderer/rt_pathtrace.hpp"
namespace raw::rt {

struct ColonnadeParams {
    double sunElevation{60.0}, sunAzimuth{65.0};   // degrees; azimuth from +x towards +z
    double turbidity{2.5};
    bool emission{true};
    int skySize{64};
};
PtScene ptColonnade(const ColonnadeParams& p = {});
PtCamera colonnadeCamera();
// The sun's direction (towards the sun) for these parameters.
Vec3 colonnadeSun(const ColonnadeParams& p);

}  // namespace raw::rt
