// Path tracer interface v3 (docs/rt-interface-v3.md; checks c2 to c7 of evidence/rt-v3-bounds.json):
// punctual lights against lighting::illuminance, the environment furnace and its control, MIS
// against escape-only, each material family against an independent quadrature of pbr::eval and
// against lighting::shadeIblReference, Beer-Lambert through a traced volume, and the v3 switch.
//   test_rt_pt_v3 [--quick]
#include "raw/renderer/rt_pathtrace.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace raw;
using namespace raw::rt;

namespace {
bool quick = false;
constexpr double kPi = 3.14159265358979323846;
using lighting::Rgb;
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
double lumf(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

// One quad, wound so its authored face points along n.
void quad(swr::Geometry& g, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n, std::uint32_t tex) {
    if (dot(cross(b - a, c - a), n) < 0.0f) std::swap(b, d);
    const std::uint32_t o = std::uint32_t(g.pos.size());
    for (Vec3 p : {a, b, c, d}) { g.pos.push_back(p); g.nrm.push_back(n); g.uv.push_back({0.5f, 0.5f}); }
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) g.idx.push_back(o + i);
    g.triTexture.push_back(tex); g.triTexture.push_back(tex);
}
PtScene base(std::uint32_t texel) {
    PtScene s;
    s.name = "v3-test";
    s.tex.add(1, 1, {texel});
    s.materials.push_back({1.0f, 1.0f, 0.0f, 0.0f, {}});   // Lambert
    return s;
}
void floorQuad(PtScene& s) { quad(s.geo, {-20, 0, -20}, {20, 0, -20}, {20, 0, 20}, {-20, 0, 20}, {0, 1, 0}, 0); }
void sphere(PtScene& s, Vec3 c, float r, int nu, int nv) {
    const std::uint32_t o = std::uint32_t(s.geo.pos.size());
    for (int j = 0; j <= nv; ++j) for (int i = 0; i <= nu; ++i) {
        const float th = float(kPi) * float(j) / float(nv), ph = 2.0f * float(kPi) * float(i) / float(nu);
        const Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        s.geo.pos.push_back(c + n * r); s.geo.nrm.push_back(n); s.geo.uv.push_back({0.5f, 0.5f});
    }
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i) {
        const std::uint32_t a = o + std::uint32_t(j * (nu + 1) + i), b = a + 1, cc = a + std::uint32_t(nu + 1), d = cc + 1;
        for (std::uint32_t k : {a, b, d, a, d, cc}) s.geo.idx.push_back(k);
        s.geo.triTexture.push_back(0); s.geo.triTexture.push_back(0);
    }
    // Outward winding: flip any triangle whose face points inward.
    for (std::size_t t = 0; t < s.geo.idx.size() / 3; ++t) {
        std::uint32_t* q = &s.geo.idx[t * 3];
        const Vec3 A = s.geo.pos[q[0]], B = s.geo.pos[q[1]], C = s.geo.pos[q[2]];
        if (dot(cross(B - A, C - A), (A + B + C) * (1.0f / 3.0f) - c) < 0.0f) std::swap(q[1], q[2]);
    }
}
lighting::Cube uniformEnv(double L) {
    lighting::Cube c; c.size = 8; c.levels = 1;
    c.rgb.assign(std::size_t(6) * 8 * 8 * 3, L);
    return c;
}
PathTraceOutput one(const PtScene& s, Vec3 eye, Vec3 target, std::uint32_t spp, std::uint32_t bounces, PtControls ctl = {}) {
    PathTraceDesc d;
    d.width = d.height = 1; d.spp = spp; d.maxBounces = bounces; d.seed = 3; d.jitter = false; d.russianRoulette = false;
    d.camera = d.previous = {eye, target, {0, 1, 0}, 0.2f};
    d.controls = ctl;
    return pathTraceCpu(s, d, 0);
}
struct Est { double mean, se; };
Est est(const PathTraceOutput& o) { return {lumf(o.radiance.data()), std::sqrt(std::max(0.0, double(o.variance[0])))}; }
Vec3 eyeAt(double deg) { const double a = deg * kPi / 180.0; return {0.0f, float(3.0 * std::cos(a)), float(3.0 * std::sin(a))}; }

void c2Punctual() {
    PtScene s = base(0xff808080u);
    floorQuad(s);
    lighting::Light pt; pt.type = lighting::LightType::Point; pt.position = {0, 3, 0}; pt.intensity = 900; pt.color = {1, 0.9, 0.8};
    lighting::Light sp = pt; sp.type = lighting::LightType::Spot; sp.position = {1, 3, 0}; sp.direction = {-0.3162278, -0.9486833, 0}; sp.innerCone = 0.1; sp.outerCone = 0.6;
    lighting::Light dl; dl.type = lighting::LightType::Directional; dl.direction = {0.6, -0.8, 0}; dl.intensity = 50;
    const double a = 128.0 / 255.0;
    for (const lighting::Light& l : {pt, sp, dl}) {
        s.lights = {l};
        finishPtScene(s);
        const PathTraceOutput o = one(s, {0, 2, 2}, {0, 0, 0}, 1, 1);
        lighting::D3 to;
        const Rgb E = lighting::illuminance(l, {0, 0, 0}, to);
        const double expect = a / kPi * E.r * to.y, got = o.radiance[0];
        const double rel = std::fabs(got - expect) / expect;
        std::printf("c2 %s: %.7g expect %.7g rel %.2e (bound 1e-4)\n", l.type == lighting::LightType::Point ? "point" : l.type == lighting::LightType::Spot ? "spot" : "directional", got, expect, rel);
        CHECK(rel <= 1e-4);
    }
    CHECK(usesV3(s));
}

void c3Furnace() {
    PtScene s = base(0xffffffffu);
    sphere(s, {0, 0, 0}, 1.0f, 64, 32);
    s.environment = uniformEnv(1.0);
    finishPtScene(s);
    const std::uint32_t spp = quick ? 2048 : 16384;
    const Est e = est(one(s, {0, 0, 4}, {0.3f, 0.2f, 0}, spp, 64));
    PtControls ctl; ctl.envMisWeightOne = true;
    const Est c = est(one(s, {0, 0, 4}, {0.3f, 0.2f, 0}, spp, 64, ctl));
    std::printf("c3 furnace: %.5f (se %.1e; bound |x - 1| <= 4 se + 2e-3); control %.4f (must exceed 1.3)\n", e.mean, e.se, c.mean);
    CHECK(std::fabs(e.mean - 1.0) <= 4.0 * e.se + 2e-3);
    CHECK(c.mean > 1.3);
}

pbr::Material family(const char* name) {
    pbr::Material m; m.baseColor = {0.8, 0.6, 0.4}; m.metallic = 0; m.roughness = 0.5;
    const std::string n = name;
    if (n == "metal") { m.metallic = 1; m.baseColor = {1.0, 0.77, 0.34}; }
    else if (n == "clearcoat") { m.metallic = 0.6; m.clearcoat = 1; m.clearcoatRoughness = 0.4; }
    else if (n == "sheen") { m.roughness = 0.8; m.sheenColor = {0.9, 0.55, 0.75}; m.sheenRoughness = 0.5; }
    else if (n == "anisotropy") { m.metallic = 1; m.roughness = 0.45; m.anisotropy = 0.8; m.anisotropyRotation = 0.3; }
    else if (n == "iridescence") { m.metallic = 1; m.roughness = 0.4; m.baseColor = {0.04, 0.04, 0.05}; m.iridescence = 1; m.iridescenceIor = 1.33; m.iridescenceThickness = 400; }
    else if (n == "thin") { m.baseColor = {0.95, 0.95, 0.95}; m.transmission = 1; m.roughness = 0.4; }
    else if (n == "volume") { m.baseColor = {0.95, 0.95, 0.95}; m.transmission = 1; m.roughness = 0.4; m.volume = true; m.thickness = 0; }
    return m;
}
// Independent reference: pbr::eval |cos| over the whole sphere by stratified uniform directions.
Est quadrature(const pbr::Material& m, const pbr::Tables& t, pbr::D3 wo, int side) {
    double s1 = 0, s2 = 0;
    for (int j = 0; j < side; ++j) for (int i = 0; i < side; ++i) {
        const double z = 1.0 - 2.0 * (j + 0.5) / side, ph = 2.0 * kPi * (i + 0.5) / side, r = std::sqrt(std::max(0.0, 1.0 - z * z));
        const pbr::D3 wi{r * std::cos(ph), r * std::sin(ph), z};
        const double v = lum(pbr::eval(m, t, wo, wi)) * std::fabs(z) * 4.0 * kPi;
        s1 += v; s2 += v * v;
    }
    const double n = double(side) * side, mean = s1 / n;
    return {mean, std::sqrt(std::max(0.0, s2 / n - mean * mean) / n)};
}

void c4c5Families() {
    const pbr::Tables tables;
    const Vec3 eye = eyeAt(40.0);
    const pbr::D3 wo{0.0, -std::sin(40.0 * kPi / 180.0), std::cos(40.0 * kPi / 180.0)};   // the floor's frame: t +x, b -z, n +y
    const std::uint32_t spp = quick ? 4096 : 32768;
    for (const char* f : {"dielectric", "metal", "clearcoat", "sheen", "anisotropy", "iridescence", "thin", "volume"}) {
        PtScene s = base(0xffffffffu);
        floorQuad(s);
        s.fullMaterials = {family(f)};
        s.environment = uniformEnv(1.0);
        finishPtScene(s);
        const Est p = est(one(s, eye, {0, 0, 0}, spp, 1));
        const Est q = quadrature(s.fullMaterials[0], tables, wo, quick ? 512 : 1024);
        const double tol = 4.0 * std::sqrt(p.se * p.se + q.se * q.se) + 1e-3;
        std::printf("c5 %-11s pt %.5f (se %.1e) quadrature %.5f (se %.1e) diff %.2e tol %.2e\n", f, p.mean, p.se, q.mean, q.se, std::fabs(p.mean - q.mean), tol);
        CHECK(std::fabs(p.mean - q.mean) <= tol);
        if (std::strcmp(f, "thin") && std::strcmp(f, "volume")) {   // c5b: the sky cube against shadeIblReference
            s.environment = lighting::cubeFrom(lighting::proceduralSky, 32, 1);
            finishPtScene(s);
            const Est ps = est(one(s, eye, {0, 0, 0}, spp, 1));
            lighting::Sample sm; sm.p = {0, 0, 0}; sm.n = {0, 1, 0}; sm.t = {1, 0, 0}; sm.v = {eye.x / 3.0, eye.y / 3.0, eye.z / 3.0}; sm.m = s.fullMaterials[0];
            const double r = lum(lighting::shadeIblReference(sm, tables, s.environment, quick ? 1 << 13 : 1 << 15));
            std::printf("c5b %-10s pt %.2f (se %.1e) reference %.2f rel %.2e (bound 3e-2)\n", f, ps.mean, ps.se, r, std::fabs(ps.mean - r) / r);
            CHECK(std::fabs(ps.mean - r) <= 0.03 * r);
            if (!std::strcmp(f, "dielectric") || !std::strcmp(f, "metal")) {   // c4: MIS against escape-only
                PtControls ctl; ctl.envNoNee = true;
                const Est e = est(one(s, eye, {0, 0, 0}, spp * 4, 1, ctl));
                const double t4 = 4.0 * std::sqrt(ps.se * ps.se + e.se * e.se);
                std::printf("c4 %-10s MIS %.2f escape-only %.2f (se %.1e) diff %.2e tol %.2e\n", f, ps.mean, e.mean, e.se, std::fabs(ps.mean - e.mean), t4);
                CHECK(std::fabs(ps.mean - e.mean) <= t4);
            }
        }
    }
}

// The glass box: the environment is 1 inside a 60-degree cone behind the box (-z), 0 elsewhere.
Rgb behind(lighting::D3 d) { return d.z < -0.5 ? Rgb{1, 1, 1} : Rgb{0, 0, 0}; }
double glassThrough(float thick) {
    PtScene s = base(0xffffffffu);
    const float h = 20.0f, z0 = 0.0f, z1 = -thick;
    quad(s.geo, {-h, -h, z0}, {h, -h, z0}, {h, h, z0}, {-h, h, z0}, {0, 0, 1}, 0);
    quad(s.geo, {-h, -h, z1}, {h, -h, z1}, {h, h, z1}, {-h, h, z1}, {0, 0, -1}, 0);
    quad(s.geo, {-h, -h, z1}, {-h, -h, z0}, {-h, h, z0}, {-h, h, z1}, {-1, 0, 0}, 0);
    quad(s.geo, {h, -h, z1}, {h, -h, z0}, {h, h, z0}, {h, h, z1}, {1, 0, 0}, 0);
    quad(s.geo, {-h, h, z1}, {h, h, z1}, {h, h, z0}, {-h, h, z0}, {0, 1, 0}, 0);
    quad(s.geo, {-h, -h, z1}, {h, -h, z1}, {h, -h, z0}, {-h, -h, z0}, {0, -1, 0}, 0);
    pbr::Material g; g.baseColor = {1, 1, 1}; g.metallic = 0; g.roughness = 0.05; g.transmission = 1; g.volume = true; g.ior = 1.5;
    g.attenuationColor = {0.5, 0.5, 0.5}; g.attenuationDistance = 1.0; g.thickness = 1.0;
    s.fullMaterials = {g};
    s.environment = lighting::cubeFrom(behind, 16, 1);
    finishPtScene(s);
    return est(one(s, {0, 0, 3}, {0, 0, 0}, quick ? 1024 : 4096, 16)).mean;
}
void c6Beer() {
    const double a = glassThrough(0.25f), b = glassThrough(0.75f), ratio = b / a, expect = std::sqrt(0.5);
    std::printf("c6 glass box: %.5f / %.5f = %.5f expect %.5f rel %.2e (bound 2e-2)\n", b, a, ratio, expect, std::fabs(ratio - expect) / expect);
    CHECK(a > 0.5);   // most of the light gets through two interfaces and 0.25 m
    CHECK(std::fabs(ratio - expect) <= 0.02 * expect);
}

void c7Switch() {
    PtScene v2 = base(0xffffffffu);
    floorQuad(v2);
    CHECK(!usesV3(v2));
    PtScene a = v2; a.lights.push_back({}); CHECK(usesV3(a));
    PtScene b = v2; b.environment = uniformEnv(1); CHECK(usesV3(b));
    PtScene c = v2; c.fullMaterials.push_back({}); CHECK(usesV3(c));
    CHECK(kPathTraceVersion == 3);
}
}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--quick")) quick = true;
    c7Switch();
    c2Punctual();
    c3Furnace();
    c4c5Families();
    c6Beer();
    return raw_test_summary();
}
