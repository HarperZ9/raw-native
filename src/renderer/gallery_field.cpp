// The sphere field (ROADMAP P10; evidence/p10-sphere-field-bounds.json): see raw/renderer/gallery.hpp.
#include "raw/renderer/gallery.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gallery {
namespace {
using lighting::D3;
D3 unit(D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return {a.x / l, a.y / l, a.z / l}; }
constexpr double kGroundY = -1.0, kR = 0.6, kFieldFovy = 0.7, kGroundHalf = 80.0, kGroundFar = -150.0;
constexpr int kLooks = 12;

// Look k, variant v (0 or 1), and its label.
pbr::Material look(int k, int v, std::string* label) {
    pbr::Material m;
    m.metallic = 0; m.roughness = 0.5;
    const char* name = "";
    switch (k) {
    case 0: m.baseColor = {1.0, 0.766, 0.336}; m.metallic = 1; m.roughness = v ? 0.4 : 0.1; name = "gold"; break;
    case 1: m.baseColor = {0.955, 0.638, 0.538}; m.metallic = 1; m.roughness = v ? 0.6 : 0.25; name = "copper"; break;
    case 2: m.baseColor = {0.75, 0.06, 0.05}; m.roughness = v ? 0.5 : 0.05; name = "red plastic"; break;
    case 3: m.baseColor = {0.9, 0.9, 0.88}; m.roughness = v ? 0.8 : 0.3; name = "white ceramic"; break;
    case 4: m.baseColor = {0.05, 0.12, 0.45}; m.metallic = 0.6; m.roughness = 0.45; m.clearcoat = 1; m.clearcoatRoughness = v ? 0.3 : 0.03; name = "car paint (clearcoat)"; break;
    case 5: m.baseColor = {0.3, 0.02, 0.05}; m.roughness = 0.8; m.sheenColor = {0.95, 0.5, 0.6}; m.sheenRoughness = v ? 0.8 : 0.3; name = "velvet (sheen)"; break;
    case 6: m.baseColor = {0.8, 0.8, 0.82}; m.metallic = 1; m.roughness = 0.35; m.anisotropy = v ? 0.9 : 0.5; name = "brushed steel (anisotropy)"; break;
    case 7: m.baseColor = {0.04, 0.04, 0.05}; m.metallic = 1; m.roughness = 0.15; m.iridescence = 1; m.iridescenceIor = 1.33; m.iridescenceThickness = v ? 700 : 300; name = "thin film (iridescence)"; break;
    case 8: m.baseColor = {0.98, 0.98, 0.98}; m.transmission = 1; m.roughness = v ? 0.3 : 0.05; name = "thin glass"; break;
    case 9: m.baseColor = {0.98, 0.98, 0.98}; m.transmission = 1; m.roughness = v ? 0.2 : 0.02; m.volume = true; m.thickness = 1.1;
            m.attenuationDistance = 0.8; m.attenuationColor = {0.55, 0.85, 0.75}; name = "volume glass (absorbing)"; break;
    case 10: m.baseColor = {0.2, 0.2, 0.2}; m.roughness = 0.6; m.emissive = {1.0, 0.55, 0.25}; m.emissiveStrength = v ? 1500 : 3000; name = "warm emissive"; break;
    default: if (v) { m.baseColor = {0.02, 0.02, 0.02}; m.roughness = 0.9; } else { m.baseColor = {0.97, 0.97, 0.97}; m.metallic = 1; m.roughness = 0.045; }
             name = "mirror and black rubber"; break;
    }
    if (label) *label = name;
    return m;
}
// World to view: the camera at kEye looking at kTarget, +Y up; s, u, b are the view axes (right,
// up, back) in world coordinates.
struct View { D3 s, u, b; };
D3 sub(D3 a, D3 c) { return {a.x - c.x, a.y - c.y, a.z - c.z}; }
D3 crossD(D3 a, D3 c) { return {a.y * c.z - a.z * c.y, a.z * c.x - a.x * c.z, a.x * c.y - a.y * c.x}; }
double dotD(D3 a, D3 c) { return a.x * c.x + a.y * c.y + a.z * c.z; }
constexpr D3 kEye{0.0, 2.4, 3.0}, kTarget{0.0, -0.5, -6.5};
View view() {
    const D3 f = unit(sub(kTarget, kEye)), s = unit(crossD(f, D3{0, 1, 0})), u = crossD(s, f);
    return {s, u, {-f.x, -f.y, -f.z}};
}
D3 toView(const View& V, D3 w) { return {dotD(V.s, w), dotD(V.u, w), dotD(V.b, w)}; }
D3 toWorld(const View& V, D3 v) { return {V.s.x * v.x + V.u.x * v.y + V.b.x * v.z, V.s.y * v.x + V.u.y * v.y + V.b.y * v.z, V.s.z * v.x + V.u.z * v.y + V.b.z * v.z}; }
// The procedural sky as the camera sees it (a capture-free function for cubeFrom).
lighting::Rgb viewSky(D3 d) { return lighting::proceduralSky(toWorld(view(), d)); }
}  // namespace

lighting::D3 fieldViewToWorld(lighting::D3 v) { return toWorld(view(), v); }

Gallery buildField(int width, int height, int spp) {
    Gallery g;
    g.width = width; g.height = height; g.spp = std::max(1, spp); g.fovy = kFieldFovy; g.name = "sphere-field";
    g.grid.fovy = kFieldFovy; g.grid.aspect = double(width) / height; g.grid.nearZ = 0.1; g.grid.farZ = 200.0;
    const View V = view();
    using lighting::Light; using lighting::LightType;
    Light sun; sun.type = LightType::Directional; sun.direction = toView(V, unit({0.6, -0.25, 0.75})); sun.intensity = 3000; sun.color = {1.0, 0.75, 0.5};
    g.lights = {sun};
    g.env = lighting::cubeFrom(viewSky, 32, 6);
    g.sh = lighting::shProject(g.env);
    g.exposure = lighting::exposure(kEv100);
    g.ground = true;
    g.groundNormal = toView(V, {0, 1, 0});
    const D3 corners[4] = {{-kGroundHalf, kGroundY, kGroundFar}, {-kGroundHalf, kGroundY, 8.0}, {kGroundHalf, kGroundY, 8.0}, {kGroundHalf, kGroundY, kGroundFar}};
    for (int i = 0; i < 4; ++i) g.groundQuad[i] = toView(V, sub(corners[i], kEye));
    g.groundMaterial.baseColor = {0.35, 0.33, 0.30}; g.groundMaterial.metallic = 0; g.groundMaterial.roughness = 0.7;
    // Four rows of six; idx = 5 (6 row + col) mod 24 is a permutation, so each look appears twice, spread out.
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 6; ++col) {
        const int idx = ((row * 6 + col) * 5) % 24, k = idx % kLooks, v = idx / kLooks;
        const D3 c{(col - 2.5) * 1.45 + (row % 2) * 0.35, kGroundY + kR, -3.5 - 2.4 * row};
        g.spheres.push_back({toView(V, sub(c, kEye)), kR, look(k, v, nullptr), k});
    }
    for (int k = 0; k < kLooks; ++k) { std::string l; look(k, 0, &l); g.groupLabels.push_back(l); }
    g.groupLabels.push_back("ground");
    g.background.assign(std::size_t(width) * height, {});
    g.hits.assign(std::size_t(width) * height, 0);
    const double th = std::tan(0.5 * kFieldFovy), inv = 1.0 / (g.spp * g.spp);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int sy = 0; sy < g.spp; ++sy) for (int sx = 0; sx < g.spp; ++sx) {
        const double u = (x + (sx + 0.5) / g.spp) / width * 2.0 - 1.0, v = 1.0 - (y + (sy + 0.5) / g.spp) / height * 2.0;
        const D3 dir = unit({u * th * g.grid.aspect, v * th, -1.0});      // view space
        const int pix = y * width + x;
        double best = 1e30; int hit = -1;
        for (std::size_t i = 0; i < g.spheres.size(); ++i) {               // in view space, the camera at the origin
            const D3 o = g.spheres[i].centre;
            const double b = dotD(dir, o), disc = b * b - (dotD(o, o) - kR * kR);
            if (disc < 0.0) continue;
            const double t = b - std::sqrt(disc);
            if (t > 0.0 && t < best) { best = t; hit = int(i); }
        }
        const D3 dw = toWorld(V, dir);
        if (dw.y < 0.0) {                                                  // the ground, found in world space
            const double t = (kGroundY - kEye.y) / dw.y, gx = kEye.x + dw.x * t, gz = kEye.z + dw.z * t;
            if (t < best && std::fabs(gx) < kGroundHalf && gz > kGroundFar && gz < 8.0) { best = t; hit = int(g.spheres.size()); }
        }
        if (hit < 0) {
            const Rgb L = g.env.sample(0, dir);
            Rgb& bg = g.background[std::size_t(pix)];
            bg = {bg.r + L.r * g.exposure * inv, bg.g + L.g * g.exposure * inv, bg.b + L.b * g.exposure * inv};
            continue;
        }
        lighting::Sample s;
        s.p = {dir.x * best, dir.y * best, dir.z * best};
        s.v = {-dir.x, -dir.y, -dir.z};
        if (hit == int(g.spheres.size())) { s.n = g.groundNormal; s.m = g.groundMaterial; g.group.push_back(kLooks); }
        else {
            const Gallery::Sphere& sp = g.spheres[std::size_t(hit)];
            s.n = unit(sub(s.p, sp.centre)); s.m = sp.m; g.group.push_back(sp.group);
        }
        const D3 t = crossD(s.n, D3{0, 1, 0});                             // path tracer v3's tangent rule, in view space
        s.t = dotD(t, t) > 1e-12 ? unit(t) : unit(D3{1.0 - s.n.x * s.n.x, -s.n.x * s.n.y, -s.n.x * s.n.z});
        g.samples.push_back(s); g.pixel.push_back(pix); ++g.hits[std::size_t(pix)];
    }
    return g;
}

}  // namespace raw::gallery
