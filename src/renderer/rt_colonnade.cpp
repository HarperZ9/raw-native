// The sunlit colonnade: see raw/renderer/rt_colonnade.hpp.
#include "raw/renderer/rt_colonnade.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/renderer/sky.hpp"
#include "swr_builder.hpp"
#include <cmath>
namespace raw::rt {
namespace {
constexpr float kPi = 3.14159265f;
// A quad wound so its face points along `n` (the builder takes the winding as given).
void quadFacing(swr::Builder& b, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n, std::uint32_t tex) {
    if (dot(cross(p1 - p0, p3 - p0), n) < 0.0f) b.quad(p0, p3, p2, p1, 1, 1, tex);
    else b.quad(p0, p1, p2, p3, 1, 1, tex);
}
// A semicircular arch between two columns: the front and back faces fill the spandrel up to a square
// of half-size R around the springing centre, and the soffit is the inner half-cylinder of radius r.
void arch(swr::Builder& b, float xc, float yc, float r, float R, float zf, float zb, int seg, std::uint32_t tex) {
    const auto outer = [&](float a) { const float c = std::cos(a), s = std::sin(a), m = std::max(std::fabs(c), std::fabs(s)); return Vec3{xc + R * c / m, yc + R * s / m, 0}; };
    for (int i = 0; i < seg; ++i) {
        const float a0 = kPi * float(i) / float(seg), a1 = kPi * float(i + 1) / float(seg);
        const Vec3 i0{xc + r * std::cos(a0), yc + r * std::sin(a0), 0}, i1{xc + r * std::cos(a1), yc + r * std::sin(a1), 0}, o0 = outer(a0), o1 = outer(a1);
        const auto at = [](Vec3 v, float z) { return Vec3{v.x, v.y, z}; };
        quadFacing(b, at(i0, zf), at(o0, zf), at(o1, zf), at(i1, zf), {0, 0, 1}, tex);
        quadFacing(b, at(i0, zb), at(o0, zb), at(o1, zb), at(i1, zb), {0, 0, -1}, tex);
        const float am = 0.5f * (a0 + a1);
        quadFacing(b, at(i0, zf), at(i1, zf), at(i1, zb), at(i0, zb), {-std::cos(am), -std::sin(am), 0}, tex);
    }
}
void sphere(swr::Geometry& g, Vec3 c, float r, int nu, int nv, std::uint32_t tex) {
    const std::uint32_t o = std::uint32_t(g.pos.size());
    for (int j = 0; j <= nv; ++j) for (int i = 0; i <= nu; ++i) {
        const float th = kPi * float(j) / float(nv), ph = 2.0f * kPi * float(i) / float(nu);
        const Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        g.pos.push_back(c + n * r); g.nrm.push_back(n); g.uv.push_back({0.5f, 0.5f});
    }
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i) {
        const std::uint32_t a = o + std::uint32_t(j * (nu + 1) + i), b = a + 1, cc = a + std::uint32_t(nu + 1), d = cc + 1;
        std::uint32_t tri[2][3] = {{a, cc, d}, {a, d, b}};
        for (auto& q : tri) {
            const Vec3 A = g.pos[q[0]], B = g.pos[q[1]], C = g.pos[q[2]];
            if (length(cross(B - A, C - A)) <= 0.0f) continue;
            if (dot(cross(B - A, C - A), (A + B + C) * (1.0f / 3.0f) - c) < 0.0f) std::swap(q[1], q[2]);
            for (std::uint32_t k : q) g.idx.push_back(k);
            g.triTexture.push_back(tex);
        }
    }
}
}  // namespace

Vec3 colonnadeSun(const ColonnadeParams& p) {
    const double a = p.sunElevation * 3.14159265358979323846 / 180.0, z = p.sunAzimuth * 3.14159265358979323846 / 180.0;
    return {float(std::cos(a) * std::cos(z)), float(std::sin(a)), float(std::cos(a) * std::sin(z))};
}
PtCamera colonnadeCamera() { return {{6.5f, 1.7f, 7.5f}, {-0.3f, 1.6f, -0.5f}, {0.0f, 1.0f, 0.0f}, 0.85f}; }

PtScene ptColonnade(const ColonnadeParams& p) {
    PtScene s;
    s.name = "colonnade";
    const auto material = [&](pbr::Material m) {
        const std::uint32_t tex = s.tex.add(1, 1, {0xffffffffu});
        s.materials.push_back({1.0f, 1.0f, 0.0f, 0.0f, {}});
        s.fullMaterials.push_back(m);
        return tex;
    };
    pbr::Material stone; stone.baseColor = {0.55, 0.50, 0.44}; stone.metallic = 0; stone.roughness = 0.8;
    pbr::Material marble; marble.baseColor = {0.82, 0.80, 0.76}; marble.metallic = 0; marble.roughness = 0.4;
    pbr::Material terracotta; terracotta.baseColor = {0.70, 0.40, 0.30}; terracotta.metallic = 0; terracotta.roughness = 0.9;
    pbr::Material mirror; mirror.baseColor = {0.95, 0.95, 0.95}; mirror.metallic = 1; mirror.roughness = 0.045;
    pbr::Material copper; copper.baseColor = {0.955, 0.638, 0.538}; copper.metallic = 1; copper.roughness = 0.5;
    pbr::Material glow; glow.baseColor = {0.2, 0.2, 0.2}; glow.metallic = 0; glow.roughness = 0.6;
    if (p.emission) { glow.emissive = {1.0, 0.5, 0.2}; glow.emissiveStrength = 20000.0; }
    const std::uint32_t tStone = material(stone), tMarble = material(marble), tWall = material(terracotta), tMirror = material(mirror),
                        tCopper = material(copper), tGlow = material(glow);
    swr::Builder b{s.geo};
    b.quad({-200, 0, 200}, {200, 0, 200}, {200, 0, -200}, {-200, 0, -200}, 1, 1, tStone);      // the floor, facing up
    b.box({-8.0f, 0.0f, -3.8f}, {8.0f, 5.0f, -3.5f}, tWall);                                   // the back wall
    const float yc = 2.6f, colR = 0.25f, r = 0.75f, R = 1.0f, zf = 0.35f, zb = -0.35f;
    for (int k = 0; k < 6; ++k) b.cylinder({-5.0f + 2.0f * float(k), 0.0f, 0.0f}, colR, yc, 24, tMarble);
    for (int k = 0; k < 5; ++k) arch(b, -4.0f + 2.0f * float(k), yc, r, R, zf, zb, 16, tMarble);
    b.box({-5.4f, yc + R, zb - 0.05f}, {5.4f, yc + R + 0.5f, zf + 0.05f}, tMarble);              // the entablature
    sphere(s.geo, {-2.2f, 0.6f, 2.2f}, 0.6f, 64, 32, tMirror);
    sphere(s.geo, {2.2f, 0.6f, 2.0f}, 0.6f, 64, 32, tCopper);
    sphere(s.geo, {0.3f, 0.35f, 1.2f}, 0.35f, 48, 24, tGlow);
    // The sky (no disk) as the environment, the sun as a directional light along it.
    sky::Clear cl; cl.turbidity = p.turbidity;
    const Vec3 sun = colonnadeSun(p);
    cl.sun = {sun.x, sun.y, sun.z};
    lighting::Cube env; env.size = p.skySize; env.levels = 1; env.rgb.assign(env.levelOffset(1), 0.0);
    for (int f = 0; f < 6; ++f) for (int y = 0; y < env.size; ++y) for (int x = 0; x < env.size; ++x) {
        const lighting::Rgb v = sky::preetham(cl, lighting::cube::dirOf(f, (x + 0.5) / env.size, (y + 0.5) / env.size));
        const std::size_t i = ((std::size_t(f) * env.size + y) * env.size + x) * 3;
        env.rgb[i] = v.r; env.rgb[i + 1] = v.g; env.rgb[i + 2] = v.b;
    }
    s.environment = env;
    const lighting::Rgb E = sky::sunIlluminance(cl);
    lighting::Light L; L.type = lighting::LightType::Directional; L.direction = {-sun.x, -sun.y, -sun.z};
    L.intensity = E.g; L.color = {E.r / E.g, 1.0, E.b / E.g};
    s.lights = {L};
    finishPtScene(s);
    return s;
}

}  // namespace raw::rt
