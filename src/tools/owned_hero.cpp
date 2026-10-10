// raw-hero: a helmet-class asset carrying every material extension. See raw/tools/owned_assets.hpp.
// Units are metres; the helmet is about 0.33 m wide, centred on the vertical axis, its rim at y = 0.
#include "owned_build.hpp"
#include <cmath>
namespace raw::owned {
namespace {
using namespace build;
constexpr double kRx = 0.165, kRy = 0.19, kRz = 0.18;     // shell semi-axes
constexpr double kOpen = 0.13;                             // front opening half-width, in turns
constexpr double kVTop = 0.0, kVRim = 0.6;                 // polar range of the shell, in half turns

// The shell's surface at radius scale s: angle a around (a = 0 faces +z), polar t from the top.
P3 shellAt(double a, double t, double s) {
    const double st = dsin(t);
    return {s * kRx * st * dsin(a), s * kRy * dcos(t) - kRy * dcos(kVRim * kPi), s * kRz * st * dcos(a)};
}
pbr::Rgb rgb(double r, double g, double b) { return {r, g, b}; }

Part shell() {
    Part p; p.name = "shell"; p.kind = "shell";
    p.material.baseColor = rgb(0.32, 0.36, 0.42); p.material.metallic = 1.0; p.material.roughness = 0.38;
    p.material.anisotropy = 0.6; p.material.anisotropyRotation = 0.0;
    p.material.clearcoat = 1.0; p.material.clearcoatRoughness = 0.06;
    p.extensions = kClearcoat | kAnisotropy;
    // Outer surface, around the back from one side of the opening to the other.
    grid(p, [](double u, double v) {
        const double a = 2.0 * kPi * (kOpen + u * (1.0 - 2.0 * kOpen)), t = kPi * (0.025 + v * (kVRim - 0.025));
        return shellAt(a, t, 1.0);
    }, 220, 110, false, true);
    // The forehead above the opening, so the opening is a window.
    grid(p, [](double u, double v) {
        const double a = 2.0 * kPi * (-kOpen + u * 2.0 * kOpen), t = kPi * (0.025 + v * (0.17 - 0.025));
        return shellAt(a, t, 1.0);
    }, 60, 30, false, true);
    return p;
}
Part liner() {
    Part p; p.name = "liner"; p.kind = "liner";
    p.material.baseColor = rgb(0.10, 0.09, 0.08); p.material.metallic = 0.0; p.material.roughness = 0.9;
    p.material.sheenColor = rgb(0.75, 0.62, 0.48); p.material.sheenRoughness = 0.5;
    p.extensions = kSheen;
    grid(p, [](double u, double v) {
        const double a = 2.0 * kPi * (kOpen + u * (1.0 - 2.0 * kOpen)), t = kPi * (0.03 + v * (kVRim - 0.03));
        return shellAt(a, t, 0.93);
    }, 160, 80, false, false);   // faces inward
    return p;
}
Part visor() {
    Part p; p.name = "visor"; p.kind = "visor";
    p.material.baseColor = rgb(0.95, 0.95, 0.95); p.material.metallic = 0.0; p.material.roughness = 0.04;
    p.material.ior = 1.5; p.material.transmission = 1.0;
    p.material.volume = true; p.material.thickness = 0.004; p.material.attenuationColor = rgb(0.95, 0.72, 0.35); p.material.attenuationDistance = 0.05;
    p.material.iridescence = 0.6; p.material.iridescenceIor = 1.33; p.material.iridescenceThickness = 380.0;
    p.extensions = kIor | kTransmission | kVolume | kIridescence;
    for (int side = 0; side < 2; ++side) {   // outer and inner faces, 4 mm apart
        const double s = side == 0 ? 1.02 : 0.995;
        grid(p, [s](double u, double v) {
            const double a = 2.0 * kPi * (-kOpen - 0.01 + u * 2.0 * (kOpen + 0.01)), t = kPi * (0.165 + v * (0.5 - 0.165));
            return shellAt(a, t, s);
        }, 90, 60, false, side == 0);
    }
    return p;
}
Part seals() {
    Part p; p.name = "seals"; p.kind = "seal";
    p.material.baseColor = rgb(0.03, 0.03, 0.03); p.material.metallic = 0.0; p.material.roughness = 0.55;
    p.material.ior = 1.52; p.material.specular = 0.5;
    p.extensions = kIor | kSpecular;
    // A tube of radius 1.4 cm along the rim, around the back.
    grid(p, [](double u, double v) {
        const double a = 2.0 * kPi * (kOpen + u * (1.0 - 2.0 * kOpen)), t = kPi * kVRim;
        const P3 c = shellAt(a, t, 0.965);
        const P3 out = unit(P3{dsin(a), 0.0, dcos(a)});
        const double w = 2.0 * kPi * v, r = 0.014;
        return c + out * (r * dcos(w)) + P3{0, 1, 0} * (r * dsin(w));
    }, 160, 24, false, false);
    return p;
}
Part trim() {
    Part p; p.name = "trim"; p.kind = "trim";
    p.material.baseColor = rgb(1.0, 0.78, 0.34); p.material.metallic = 1.0; p.material.roughness = 0.22;
    // A raised band over the crown, front to back, 5 cm wide.
    grid(p, [](double u, double v) {
        const double t = kPi * (-0.58 + u * (0.58 + 0.16)), x = -0.025 + v * 0.05;
        const double sy = 1.02 * kRy * dcos(t) - kRy * dcos(kVRim * kPi), sz = 1.02 * kRz * dsin(t);
        return P3{x, sy, sz};
    }, 140, 8, false, false);
    return p;
}
Part vents() {
    Part p; p.name = "vents"; p.kind = "vent";
    p.material.baseColor = rgb(0.06, 0.07, 0.08); p.material.metallic = 0.0; p.material.roughness = 0.45;
    for (int side = -1; side <= 1; side += 2)
        for (int k = 0; k < 4; ++k) {
            // On the shell's side at height y: cos t from the shell's height formula, then its radius.
            const double y = 0.06 + 0.035 * k, ct = y / kRy + dcos(kVRim * kPi), st = std::sqrt(1.0 - ct * ct);
            const double x = side * (kRx * st + 0.004), z = -0.03 - 0.012 * k;
            box(p, {x - 0.012, y - 0.012, z - 0.045}, {x + 0.012, y + 0.012, z + 0.045});
        }
    return p;
}
Part lights() {
    Part p; p.name = "lights"; p.kind = "light";
    p.material.baseColor = rgb(0.0, 0.0, 0.0); p.material.metallic = 0.0; p.material.roughness = 0.3;
    p.material.emissive = rgb(1.0, 0.55, 0.12); p.material.emissiveStrength = 40.0;
    p.extensions = kEmissiveStrength;
    for (int side = 0; side < 2; ++side)
        grid(p, [side](double u, double v) {
            const double a0 = side == 0 ? 0.18 : 0.62, a = 2.0 * kPi * (a0 + u * 0.2), t = kPi * (0.33 + v * 0.06);
            return shellAt(a, t, 1.012);
        }, 80, 6, false, true);
    return p;
}
}  // namespace

Asset hero() {
    Asset a; a.name = "raw-hero";
    a.parts = {shell(), liner(), visor(), seals(), trim(), vents(), lights()};
    return a;
}
}  // namespace raw::owned
