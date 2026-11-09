// The spectral caustic tracer's measured inputs, the prism room and the packing (raw/renderer/rt_caustic.hpp).
#include "rt_caustic_math.hpp"
#include "data/cie1931.hpp"
#include <cmath>
namespace raw::rt {
namespace {
constexpr const char* kRii = "refractiveindex.info database release v2026-05-24 (CC0), SCHOTT catalogue formula 2; "
                             "web/shaders/lens/glass-data.mjs, rii-database-2026-05-24.zip sha256 6cdebebd17fe02b6dc2b03189e15e6c689afc6619c558e1df7b9ac307b6d14eb";
const Glass kBK7{"schott:N-BK7", {1.03961212, 0.231792344, 1.01046945}, {0.00600069867, 0.0200179144, 103.560653}, 1.5168, 64.17, kRii};
const Glass kSF11{"schott:N-SF11", {1.73759695, 0.313747346, 1.89878101}, {0.013188707, 0.0623068142, 155.23629}, 1.78472, 25.68, kRii};

struct Builder {
    CausticScene& s;
    void tri(Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc, std::uint32_t m) {
        for (Vec3 v : {a, b, c}) s.tris.insert(s.tris.end(), {v.x, v.y, v.z});
        for (Vec3 v : {na, nb, nc}) s.normals.insert(s.normals.end(), {v.x, v.y, v.z});
        s.material.push_back(m);
    }
    void flat(Vec3 a, Vec3 b, Vec3 c, std::uint32_t m) { const Vec3 n = normalize(cross(b - a, c - a)); tri(a, b, c, n, n, n, m); }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, std::uint32_t m) { flat(a, b, c, m); flat(a, c, d, m); }
    // An axis-aligned box, faces outward.
    void box(Vec3 lo, Vec3 hi, std::uint32_t m) {
        const Vec3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                           {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        quad(p[0], p[3], p[2], p[1], m); quad(p[4], p[5], p[6], p[7], m); quad(p[0], p[4], p[7], p[3], m);
        quad(p[1], p[2], p[6], p[5], m); quad(p[3], p[7], p[6], p[2], m); quad(p[0], p[1], p[5], p[4], m);
    }
    // A prism along z: an equilateral cross-section of side `side`, base at height y0, apex up.
    void prism(float cx, float y0, float z0, float z1, float side, std::uint32_t m) {
        const float h = side * 0.8660254f;
        const Vec3 a0{cx - side / 2, y0, z0}, b0{cx + side / 2, y0, z0}, c0{cx, y0 + h, z0};
        const Vec3 a1{cx - side / 2, y0, z1}, b1{cx + side / 2, y0, z1}, c1{cx, y0 + h, z1};
        flat(a0, c0, b0, m); flat(a1, b1, c1, m);
        quad(a0, a1, c1, c0, m); quad(c0, c1, b1, b0, m); quad(b0, b1, a1, a0, m);
    }
    void lens(Vec3 c, Vec3 axis, float radius, float R, float edge, std::uint32_t m);
};
// A biconvex lens of equal radii R, aperture radius `radius` and edge thickness `edge`, centred at c
// with its axis along `axis`. Each cap is a spherical zone with analytic normals.
void Builder::lens(Vec3 c, Vec3 axis, float radius, float R, float edge, std::uint32_t m) {
    const int rings = 48, segs = 128;
    const pt::Frame f = pt::basis(normalize(axis));
    const float sag = R - std::sqrt(R * R - radius * radius), half = edge * 0.5f;
    auto capPoint = [&](float r, float phi, float side, Vec3& n) {
        const float z = std::sqrt(R * R - r * r) - (R - sag);   // height above the cap's rim plane
        const Vec3 radial = f.t * std::cos(phi) + f.b * std::sin(phi);
        const Vec3 p = c + radial * r + f.n * (side * (half + z));
        n = normalize(radial * r + f.n * (side * std::sqrt(R * R - r * r)));
        return p;
    };
    for (float side : {1.0f, -1.0f})
        for (int i = 0; i < rings; ++i)
            for (int j = 0; j < segs; ++j) {
                const float r0 = radius * float(i) / rings, r1 = radius * float(i + 1) / rings;
                const float p0 = 6.2831853f * float(j) / segs, p1 = 6.2831853f * float(j + 1) / segs;
                Vec3 n00, n01, n10, n11;
                const Vec3 a = capPoint(r0, p0, side, n00), b = capPoint(r1, p0, side, n10), cc = capPoint(r1, p1, side, n11), d = capPoint(r0, p1, side, n01);
                // ring 0 starts at the pole, where a and d coincide: one triangle per segment there
                if (side > 0) { tri(a, b, cc, n00, n10, n11, m); if (i > 0) tri(a, cc, d, n00, n11, n01, m); }
                else { tri(a, cc, b, n00, n11, n10, m); if (i > 0) tri(a, d, cc, n00, n01, n11, m); }
            }
    for (int j = 0; j < segs; ++j) {   // the cylindrical edge
        const float p0 = 6.2831853f * float(j) / segs, p1 = 6.2831853f * float(j + 1) / segs;
        const Vec3 r0 = f.t * std::cos(p0) + f.b * std::sin(p0), r1 = f.t * std::cos(p1) + f.b * std::sin(p1);
        const Vec3 a = c + r0 * radius + f.n * half, b = c + r1 * radius + f.n * half, d = c + r0 * radius - f.n * half, e = c + r1 * radius - f.n * half;
        tri(a, d, e, r0, r0, r1, m); tri(a, e, b, r0, r1, r1, m);
    }
}
CausticLight lampAt(Vec3 centre, Vec3 dir, Vec3 along, float length, float width, float coneRad, float power) {
    const Vec3 n = normalize(dir), u = normalize(along - n * dot(along, n)), v = cross(n, u);
    // eu x ev must point along n: (u * length) x (v * width) = n * length * width
    return {centre - u * (length / 2) - v * (width / 2), u * length, v * width, coneRad, power};
}
}  // namespace

double glassIndex(const Glass& g, double lambdaNm) {
    const double l2 = (lambdaNm * 1e-3) * (lambdaNm * 1e-3);
    double n2 = 1.0;
    for (int i = 0; i < 3; ++i) n2 += g.B[i] * l2 / (l2 - g.C[i]);
    return std::sqrt(n2);
}
const Glass& glassNBK7() { return kBK7; }
const Glass& glassNSF11() { return kSF11; }

CausticScene causticPrismRoom(int view, float lampScale, float coneDeg) {
    CausticScene s;
    s.name = view == 0 ? "prism_room" : "prism_room_no_glass_view";
    s.glasses = {kSF11, kBK7};
    // 0 floor, 1 walls, 2 stands, 3 prism (N-SF11), 4 lens (N-BK7), 5 lamp A, 6 lamp B, 7 the fill panel.
    // Albedos are chosen.
    s.materials = {{CausticKind::Diffuse, 0.8f, 0, 0}, {CausticKind::Diffuse, 0.6f, 0, 0}, {CausticKind::Diffuse, 0.05f, 0, 0},
                   {CausticKind::Glass, 0, 0, 0}, {CausticKind::Glass, 0, 1, 0},
                   {CausticKind::Emitter, 0, 0, 0}, {CausticKind::Emitter, 0, 0, 1}};
    Builder b{s};
    b.quad({-1.6f, 0, -1.5f}, {-1.6f, 0, 1.2f}, {1.6f, 0, 1.2f}, {1.6f, 0, -1.5f}, 0);          // floor, normal +y
    b.quad({-1.6f, 0, -1.5f}, {1.6f, 0, -1.5f}, {1.6f, 1.6f, -1.5f}, {-1.6f, 1.6f, -1.5f}, 1);  // back wall, +z
    b.quad({-1.6f, 0, -1.5f}, {-1.6f, 1.6f, -1.5f}, {-1.6f, 1.6f, 1.2f}, {-1.6f, 0, 1.2f}, 1);   // left wall, +x
    // The prism: N-SF11, side 0.12 m, base at 0.45 m, along z from -0.72 to -0.48, on a dark stand.
    const float px = -0.35f, py = 0.45f, side = 0.12f;
    b.prism(px, py, -0.72f, -0.48f, side, 3);
    b.box({px - 0.025f, 0, -0.65f}, {px + 0.025f, py, -0.55f}, 2);
    // Lamp A, a slit 0.2 m by 4 mm, 0.6 m from the prism's left face along the minimum-deviation
    // ray of N-SF11 at the d line (33.2 degrees above horizontal), cone a tenth of coneDeg (0.6 degrees by
    // default): a narrow beam on the face, so the colours separate on the floor.
    const Vec3 face{px - side / 4, py + side * 0.8660254f / 2, -0.6f}, inA{0.8368f, 0.5476f, 0};
    s.lights.push_back(lampAt(face - inA * 0.6f, inA, {0, 0, 1}, 0.2f * lampScale, 0.004f * lampScale, coneDeg * 0.1f * 0.017453293f, 240.0f));
    // The lens: N-BK7, 0.1 m aperture, radii 0.15 m, axis tilted down toward the floor, on a dark post.
    const Vec3 lc{0.75f, 0.16f, -0.2f}, axis = normalize(Vec3{-0.5f, -1.0f, 0.3f});
    b.lens(lc, axis, 0.05f, 0.15f, 0.004f, 4);
    b.box({lc.x - 0.006f, 0, lc.z - 0.006f}, {lc.x + 0.006f, lc.y - 0.05f, lc.z + 0.006f}, 2);
    // Lamp B, 12 mm square, 0.6 m up the lens axis, cone 6 degrees.
    s.lights.push_back(lampAt(lc - axis * 0.6f, axis, {0, 0, 1}, 0.012f * lampScale, 0.012f * lampScale, coneDeg * 0.017453293f, 40.0f));
    // A dim overhead fill panel, 1.2 m by 1.0 m at 1.7 m, facing down with an open cone, so the room
    // and the glass read; its own soft caustics come out of the same estimators.
    s.lights.push_back(lampAt({0.1f, 1.7f, -0.3f}, {0, -1, 0}, {1, 0, 0}, 1.2f, 1.0f, 1.5533f, 0.012f));
    s.materials.push_back({CausticKind::Emitter, 0, 0, 2});
    for (std::uint32_t k = 0; k < 3; ++k) {   // the lamps as geometry, so camera paths can see them
        const CausticLight& L = s.lights[k];
        b.quad(L.corner, L.corner + L.ev, L.corner + L.eu + L.ev, L.corner + L.eu, 5 + k);
    }
    if (view == 0) s.camera = {{0.42f, 0.9f, 0.95f}, {0.2f, 0.12f, -0.42f}, {0, 1, 0}, 0.66f};
    else s.camera = {{0.45f, 0.35f, 0.05f}, {0.45f, 0.0f, -0.6f}, {0, 1, 0}, 0.6f};
    return s;
}

namespace caustic {
double illuminantA(double l) {
    const double c2 = 1.435e7, T = 2848.0;   // c2 in nm K
    auto S = [&](double x) { return std::pow(560.0 / x, 5.0) * (std::exp(c2 / (T * 560.0)) - 1.0) / (std::exp(c2 / (T * x)) - 1.0); };
    return S(l) / S(560.0);
}
Packed pack(const CausticScene& s) {
    Packed p;
    p.tables.resize(kTablesHead);
    for (int i = 0; i < 81; ++i) {
        p.tables[kCmf + i * 3] = float(style::data::kCmfX[i]); p.tables[kCmf + i * 3 + 1] = float(style::data::kCmfY[i]);
        p.tables[kCmf + i * 3 + 2] = float(style::data::kCmfZ[i]);
        p.tables[kIllum + i] = float(illuminantA(380.0 + 5.0 * i));
    }
    p.matOff = std::uint32_t(p.tables.size());
    for (const CausticMaterial& m : s.materials) p.tables.insert(p.tables.end(), {float(std::uint32_t(m.kind)), m.albedo, float(m.glass), float(m.light)});
    p.glassOff = std::uint32_t(p.tables.size());
    for (const Glass& g : s.glasses)
        p.tables.insert(p.tables.end(), {float(g.B[0]), float(g.B[1]), float(g.B[2]), float(g.C[0]), float(g.C[1]), float(g.C[2]), float(glassIndex(g, 587.5618)), 0.0f});
    p.lightOff = std::uint32_t(p.tables.size());
    for (const CausticLight& L : s.lights) {
        const Vec3 c = cross(L.eu, L.ev), n = normalize(c);
        const float sa = std::sin(L.coneRad);
        p.tables.insert(p.tables.end(), {L.corner.x, L.corner.y, L.corner.z, L.eu.x, L.eu.y, L.eu.z, L.ev.x, L.ev.y, L.ev.z,
                                         n.x, n.y, n.z, length(c), std::cos(L.coneRad), sa * sa, L.power});
    }
    p.lights = std::uint32_t(s.lights.size());
    const std::size_t nt = s.material.size();
    p.geo.assign(nt * 12, 0.0f);
    for (std::size_t t = 0; t < nt; ++t) {
        for (int k = 0; k < 9; ++k) p.geo[t * 12 + k] = s.normals[t * 9 + k];
        p.geo[t * 12 + 9] = float(s.material[t]);
    }
    return p;
}
}  // namespace caustic

Vec3 causticLampWhite() {
    double X = 0, Y = 0, Z = 0;
    for (int i = 0; i < 81; ++i) {   // trapezoid at 5 nm, as the tracer's linear interpolation integrates
        const double w = (i == 0 || i == 80) ? 2.5 : 5.0, s = caustic::illuminantA(380.0 + 5.0 * i);
        X += w * s * style::data::kCmfX[i]; Y += w * s * style::data::kCmfY[i]; Z += w * s * style::data::kCmfZ[i];
    }
    return {float(X), float(Y), float(Z)};
}

std::vector<float> causticToRgb(const CausticImage& im, float exposure) {
    auto rgb = [](Vec3 c) {   // XYZ to linear sRGB (IEC 61966-2-1)
        return Vec3{3.2404542f * c.x - 1.5371385f * c.y - 0.4985314f * c.z, -0.9692660f * c.x + 1.8760108f * c.y + 0.0415560f * c.z,
                    0.0556434f * c.x - 0.2040259f * c.y + 1.0572252f * c.z};
    };
    const Vec3 w = rgb(causticLampWhite());
    std::vector<float> out(im.xyz.size());
    for (std::size_t p = 0; p * 3 < im.xyz.size(); ++p) {
        const Vec3 c = rgb({float(im.xyz[p * 3]), float(im.xyz[p * 3 + 1]), float(im.xyz[p * 3 + 2])});
        out[p * 3] = exposure * c.x / w.x; out[p * 3 + 1] = exposure * c.y / w.y; out[p * 3 + 2] = exposure * c.z / w.z;
    }
    return out;
}

}  // namespace raw::rt
