// The art scene's ray caster: see raw/renderer/art_scene.hpp. Six analytic primitives, nearest hit
// in float64.
#include "art_scene_prims.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
using namespace prims;
constexpr Box kWall{{-6.0, 0.0, -4.6}, {6.0, 4.0, -4.0}, 2, 4, {0.58, 0.36, 0.27}};
constexpr Box kPlinth{{-1.6, 0.0, -1.2}, {1.6, 0.9, 1.2}, 3, 2, {0.72, 0.70, 0.65}};
constexpr Sphere kBig{{0.0, 1.75, 0.0}, 0.85, 4, 3, {0.86, 0.83, 0.77}};
constexpr Sphere kSmall{{2.6, 0.6, 1.0}, 0.6, 5, 5, {0.28, 0.40, 0.62}};
constexpr Cylinder kColumn{-2.8, -1.4, 0.45, 0.0, 3.2, 6, 2, {0.66, 0.64, 0.60}};
constexpr double kGroundHalf = 30.0;
constexpr D3 kGroundAlbedo{0.34, 0.32, 0.26};

void ground(const D3& o, const D3& d, Hit& best) {
    if (std::abs(d[1]) < 1e-15) return;
    const double t = -o[1] / d[1];
    const double x = o[0] + t * d[0], z = o[2] + t * d[2];
    if (std::abs(x) <= kGroundHalf && std::abs(z) <= kGroundHalf) consider(best, t, {0, 1, 0}, 1, 1, kGroundAlbedo);
}
}  // namespace

Hit trace(const D3& o, const D3& d, SceneKind scene) {
    if (scene == SceneKind::Town) return traceTown(o, d);
    if (scene == SceneKind::Room) return traceRoom(o, d);
    Hit best;
    ground(o, d, best);
    box(kWall, o, d, best);
    box(kPlinth, o, d, best);
    sphere(kBig, o, d, best);
    sphere(kSmall, o, d, best);
    cylinder(kColumn, o, d, best);
    return best;
}

bool occluded(const D3& o, const D3& d, double tMax, SceneKind scene) {
    const Hit h = trace(o, d, scene);
    return h.t > 0 && h.t < tMax;
}

}  // namespace raw::art
