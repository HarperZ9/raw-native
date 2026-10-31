#pragma once
// Private to src/renderer: the path tracer's random numbers and BSDF, the float32 forms that
// src/renderer/gpu/shaders/pt.wgsl mirrors (raw/renderer/rt_pathtrace.hpp).
//   PCG2D: Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 2020.
//   VNDF sampling: Heitz, "Sampling the GGX Distribution of Visible Normals", JCGT 2018.
//   Orthonormal basis: Duff et al., "Building an Orthonormal Basis, Revisited", JCGT 2017.
//   Height-correlated Smith G2: Heitz, "Understanding the Masking-Shadowing Function", JCGT 2014.
//   Multiple scattering: Kulla and Conty, "Revisiting Physically Based Shading at Imageworks", 2017.
#include "raw/math/vec.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace raw::rt::pt {

inline constexpr float kPi = 3.14159265f;

inline void pcg2d(std::uint32_t& x, std::uint32_t& y) {
    x = x * 1664525u + 1013904223u; y = y * 1664525u + 1013904223u;
    x += y * 1664525u; y += x * 1664525u;
    x ^= x >> 16; y ^= y >> 16;
    x += y * 1664525u; y += x * 1664525u;
    x ^= x >> 16; y ^= y >> 16;
}
// Two uniform floats in [0, 1) for (seed, pixel, sample, dimension pair).
struct R2 { float a, b; };
inline R2 rand2(std::uint64_t seed, std::uint32_t pixel, std::uint32_t sample, std::uint32_t dim) {
    std::uint32_t x = pixel ^ (std::uint32_t(seed) * 0x9E3779B9u), y = (sample * 64u + dim) ^ std::uint32_t(seed >> 32);
    pcg2d(x, y);
    return {float(x >> 8) * 5.96046448e-8f, float(y >> 8) * 5.96046448e-8f};
}

struct Frame { Vec3 t, b, n; };
inline Frame basis(Vec3 n) {
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z), c = n.x * n.y * a;
    return {{1.0f + s * n.x * n.x * a, s * c, -s * n.x}, {c, s + n.y * n.y * a, -n.y}, n};
}
inline Vec3 toLocal(const Frame& f, Vec3 v) { return {dot(v, f.t), dot(v, f.b), dot(v, f.n)}; }
inline Vec3 toWorld(const Frame& f, Vec3 v) { return f.t * v.x + f.b * v.y + f.n * v.z; }

inline float ggxD(float a2, float cosH) {
    const float d = cosH * cosH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}
inline float lambda(float a2, float cosT) {
    const float c2 = cosT * cosT;
    return (-1.0f + std::sqrt(1.0f + a2 * (1.0f - c2) / c2)) * 0.5f;
}
inline Vec3 sampleVndf(Vec3 wo, float a, float u1, float u2) {
    const Vec3 vh = normalize(Vec3{a * wo.x, a * wo.y, wo.z});
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const Vec3 t1 = lensq > 0.0f ? Vec3{-vh.y, vh.x, 0.0f} * (1.0f / std::sqrt(lensq)) : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 t2 = cross(vh, t1);
    const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
    const float p1 = r * std::cos(phi), s = 0.5f * (1.0f + vh.z);
    const float p2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - p1 * p1)) + s * (r * std::sin(phi));
    const Vec3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
    return normalize(Vec3{a * nh.x, a * nh.y, std::max(0.0f, nh.z)});
}

// The lobe's energy table (interface v4, docs/rt-interface-v4.md): A and B, the split directional
// albedo of the white single-scattering GGX lobe (its albedo under Schlick Fresnel is F0 A + f90 B),
// on kMu columns of u = sqrt(mu) by kRough rows of roughness 0 to 1 (alpha = max(1e-4, r^2)),
// stored (A, B) a node, then each row's cosine-weighted means (Aavg, Bavg). Built once in float64
// (rt_pt_energy.cpp, with the VNDF estimator of raw/renderer/pbr.hpp's tables) and held as
// float32; the GPU tracer reads the same floats, appended to its material buffer.
struct EnergyTable {
    static constexpr int kMu = 64, kRough = 128;
    static constexpr std::size_t kNodes = std::size_t(kMu) * kRough * 2, kFloats = kNodes + std::size_t(kRough) * 2;
    std::vector<float> v;
};
const EnergyTable& energyTable();
struct AB { float a, b; };
// Bilinear in (u, roughness); pt.wgsl's tab_ab and tab_avg run the same operations.
inline AB tableAB(const float* t, float mu, float rough) {
    constexpr int N = EnergyTable::kMu, R = EnergyTable::kRough;
    const float x = std::sqrt(std::clamp(mu, 0.0f, 1.0f)) * float(N - 1), y = std::clamp(rough, 0.0f, 1.0f) * float(R - 1);
    const int x0 = std::min(int(x), N - 2), y0 = std::min(int(y), R - 2);
    const float fx = x - float(x0), fy = y - float(y0);
    const float* p = t + (std::size_t(y0) * N + std::size_t(x0)) * 2;
    const float* q = p + std::size_t(N) * 2;
    const float a0 = p[0] * (1.0f - fx) + p[2] * fx, b0 = p[1] * (1.0f - fx) + p[3] * fx;
    const float a1 = q[0] * (1.0f - fx) + q[2] * fx, b1 = q[1] * (1.0f - fx) + q[3] * fx;
    return {a0 * (1.0f - fy) + a1 * fy, b0 * (1.0f - fy) + b1 * fy};
}
inline AB tableAvg(const float* t, float rough) {
    constexpr int R = EnergyTable::kRough;
    const float y = std::clamp(rough, 0.0f, 1.0f) * float(R - 1);
    const int y0 = std::min(int(y), R - 2);
    const float fy = y - float(y0);
    const float* p = t + EnergyTable::kNodes + std::size_t(y0) * 2;
    return {p[0] * (1.0f - fy) + p[2] * fy, p[1] * (1.0f - fy) + p[3] * fy};
}

// legacy: interface v3's lobe, the control of the v4 energy checks (PtControls::legacyLobeV2).
struct Bsdf { Vec3 base; float rough, metallic, specular; bool legacy{false}; };
inline float lobeProb(const Bsdf& m) { return m.metallic + (1.0f - m.metallic) * 0.5f * m.specular; }
inline Vec3 f0(const Bsdf& m) {
    const float d = 0.04f * m.specular;
    return {d + (m.base.x - d) * m.metallic, d + (m.base.y - d) * m.metallic, d + (m.base.z - d) * m.metallic};
}
// Kulla and Conty 2017: the share of the white multiple-scattering lobe that a Fresnel of mean
// favg keeps (raw/renderer/pbr.hpp uses the same form).
inline float kms(float favg, float eavg) { return favg * favg * eavg / (1.0f - favg * (1.0f - eavg)); }
// Interface v4's energy terms at one pair of cosines (raw/renderer/pbr.hpp's model without its
// extensions). ms: the white multiple-scattering lobe (1 - E(mu_o))(1 - E(mu_i)) / (pi (1 - Eavg)).
// kd: the dielectric layer's share of it. wd: the Lambert base's weight
// (1 - Es(mu_o))(1 - Es(mu_i)) / (1 - Es_avg), with Es the dielectric layer's directional albedo
// (F0 0.04 specular, f90 1 while specular > 0), single and multiple scattering together.
// A white base then reflects 1 at every view, metal or dielectric.
struct Energy { float ms, wd, kd, eavg; };
inline Energy energyOf(const Bsdf& m, float muO, float muI) {
    const float* t = energyTable().v.data();
    const AB o = tableAB(t, muO, m.rough), i = tableAB(t, muI, m.rough), av = tableAvg(t, m.rough);
    const float eo = std::min(1.0f, o.a + o.b), ei = std::min(1.0f, i.a + i.b), ea = std::min(1.0f, av.a + av.b);
    Energy e;
    e.eavg = ea;
    e.ms = ea < 1.0f ? (1.0f - eo) * (1.0f - ei) / (kPi * (1.0f - ea)) : 0.0f;
    const float on = m.specular > 0.0f ? 1.0f : 0.0f, d = 0.04f * m.specular;
    e.kd = kms(on * (d + (1.0f - d) / 21.0f), ea);
    const float eso = on * (d * o.a + o.b) + e.kd * (1.0f - eo), esi = on * (d * i.a + i.b) + e.kd * (1.0f - ei);
    const float esa = on * (d * av.a + av.b) + e.kd * (1.0f - ea);
    e.wd = (1.0f - eso) * (1.0f - esi) / (1.0f - esa);
    return e;
}
// f(wo, wi) cos(wi) and the one-sample pdf of choosing wi, both in the local frame.
struct Eval { Vec3 fcos; float pdf; };
// Interface v3's lobe: Lambert plus single-scattering GGX, added (the v4 control).
inline Eval evalLegacy(const Bsdf& m, Vec3 wo, Vec3 wi, bool dropCosine, bool ndfPdf) {
    if (wo.z <= 0.0f || wi.z <= 0.0f) return {{0, 0, 0}, 0.0f};
    const float ps = lobeProb(m), cosI = dropCosine ? 1.0f : wi.z;
    const float kd = (1.0f - m.metallic) / kPi;
    Vec3 f{m.base.x * kd, m.base.y * kd, m.base.z * kd};
    float pdf = (1.0f - ps) * wi.z / kPi;
    if (ps > 0.0f) {
        const float a = std::max(1e-4f, m.rough * m.rough), a2 = a * a;
        const Vec3 h = normalize(wo + wi);
        const float oh = dot(wo, h), D = ggxD(a2, h.z);
        const float G2 = 1.0f / (1.0f + lambda(a2, wo.z) + lambda(a2, wi.z)), G1 = 1.0f / (1.0f + lambda(a2, wo.z));
        const float x1 = std::clamp(1.0f - oh, 0.0f, 1.0f);   // pow(1 - oh, 5) on the GPU is exp2 of a log: a base rounded below 0 gave NaN
        const float fw = x1 * x1 * x1 * x1 * x1;
        const Vec3 F0 = f0(m);
        const Vec3 F{F0.x + (1.0f - F0.x) * fw, F0.y + (1.0f - F0.y) * fw, F0.z + (1.0f - F0.z) * fw};
        const float spec = D * G2 / (4.0f * wo.z * wi.z);
        f = f + F * spec;
        pdf += ps * (ndfPdf ? D * h.z / (4.0f * std::fabs(oh)) : G1 * D / (4.0f * wo.z));
    }
    return {f * cosI, pdf};
}
// Interface v4's lobe: metal and dielectric mixed by metallic. The specular layer is GGX with
// height-correlated Smith and Schlick Fresnel (f90 1 for metal, and for the dielectric while
// specular > 0) plus Kulla-Conty multiple scattering; the dielectric's Lambert base sits under
// it, weighted by Energy::wd. Reciprocal. A material with no specular lobe (metallic 0 and
// specular 0) stays pure Lambert, by v3's operations. Sampling and pdf are v3's.
inline Eval evalBsdf(const Bsdf& m, Vec3 wo, Vec3 wi, bool dropCosine = false, bool ndfPdf = false) {
    if (m.legacy) return evalLegacy(m, wo, wi, dropCosine, ndfPdf);
    if (wo.z <= 0.0f || wi.z <= 0.0f) return {{0, 0, 0}, 0.0f};
    const float ps = lobeProb(m), cosI = dropCosine ? 1.0f : wi.z;
    const float kd = (1.0f - m.metallic) / kPi;
    Vec3 f{m.base.x * kd, m.base.y * kd, m.base.z * kd};
    float pdf = (1.0f - ps) * wi.z / kPi;
    if (ps > 0.0f) {
        const Energy en = energyOf(m, wo.z, wi.z);
        f = f * en.wd;
        const float a = std::max(1e-4f, m.rough * m.rough), a2 = a * a;
        const Vec3 h = normalize(wo + wi);
        const float oh = dot(wo, h), D = ggxD(a2, h.z);
        const float G2 = 1.0f / (1.0f + lambda(a2, wo.z) + lambda(a2, wi.z)), G1 = 1.0f / (1.0f + lambda(a2, wo.z));
        const float x1 = std::clamp(1.0f - oh, 0.0f, 1.0f);
        const float fw = x1 * x1 * x1 * x1 * x1;
        const Vec3 F0 = f0(m);
        const float f90 = m.metallic + (1.0f - m.metallic) * (m.specular > 0.0f ? 1.0f : 0.0f);
        const Vec3 F{F0.x + (f90 - F0.x) * fw, F0.y + (f90 - F0.y) * fw, F0.z + (f90 - F0.z) * fw};
        const float spec = D * G2 / (4.0f * wo.z * wi.z);
        const auto km = [&](float b) { return m.metallic * kms(b + (1.0f - b) / 21.0f, en.eavg) + (1.0f - m.metallic) * en.kd; };
        f = f + F * spec + Vec3{km(m.base.x), km(m.base.y), km(m.base.z)} * en.ms;
        pdf += ps * (ndfPdf ? D * h.z / (4.0f * std::fabs(oh)) : G1 * D / (4.0f * wo.z));
    }
    return {f * cosI, pdf};
}
// The Lambert term of evalBsdf alone, fd cos, by the same operations (interface v2's diffuse;
// v4 weights it by Energy::wd where a specular lobe exists).
inline Vec3 lambertCos(const Bsdf& m, Vec3 wo, Vec3 wi, bool dropCosine = false) {
    if (wo.z <= 0.0f || wi.z <= 0.0f) return {0, 0, 0};
    const float cosI = dropCosine ? 1.0f : wi.z, kd = (1.0f - m.metallic) / kPi;
    Vec3 f{m.base.x * kd, m.base.y * kd, m.base.z * kd};
    if (!m.legacy && lobeProb(m) > 0.0f) f = f * energyOf(m, wo.z, wi.z).wd;
    return f * cosI;
}
// Interface v2's specular albedo estimate: F0 + (1 - F0)(1 - cos_o)^5 where the lobe exists.
inline Vec3 specAlbedo(const Bsdf& m, float cosO) {
    if (lobeProb(m) <= 0.0f) return {0, 0, 0};
    const float x1 = std::clamp(1.0f - cosO, 0.0f, 1.0f), fw = x1 * x1 * x1 * x1 * x1;
    const Vec3 F0 = f0(m);
    return {F0.x + (1.0f - F0.x) * fw, F0.y + (1.0f - F0.y) * fw, F0.z + (1.0f - F0.z) * fw};
}
// Sample wi from the one-sample mixture; false when the sample leaves the hemisphere.
inline bool sampleBsdf(const Bsdf& m, Vec3 wo, float pick, float u1, float u2, Vec3& wi) {
    if (pick < lobeProb(m)) {
        const float a = std::max(1e-4f, m.rough * m.rough);
        const Vec3 h = sampleVndf(wo, a, u1, u2);
        wi = h * (2.0f * dot(wo, h)) - wo;
    } else {
        const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
        wi = {r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1))};
    }
    return wi.z > 0.0f;
}
inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

}  // namespace raw::rt::pt
