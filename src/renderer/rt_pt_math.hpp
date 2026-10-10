#pragma once
// Private to src/renderer: the path tracer's random numbers and BSDF, the float32 forms that
// src/renderer/gpu/shaders/pt.wgsl mirrors (raw/renderer/rt_pathtrace.hpp).
//   PCG2D: Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 2020.
//   VNDF sampling: Heitz, "Sampling the GGX Distribution of Visible Normals", JCGT 2018.
//   Orthonormal basis: Duff et al., "Building an Orthonormal Basis, Revisited", JCGT 2017.
//   Height-correlated Smith G2: Heitz, "Understanding the Masking-Shadowing Function", JCGT 2014.
#include "raw/math/vec.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
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

struct Bsdf { Vec3 base; float rough, metallic, specular; };
inline float lobeProb(const Bsdf& m) { return m.metallic + (1.0f - m.metallic) * 0.5f * m.specular; }
inline Vec3 f0(const Bsdf& m) {
    const float d = 0.04f * m.specular;
    return {d + (m.base.x - d) * m.metallic, d + (m.base.y - d) * m.metallic, d + (m.base.z - d) * m.metallic};
}
// f(wo, wi) cos(wi) and the one-sample pdf of choosing wi, both in the local frame.
struct Eval { Vec3 fcos; float pdf; };
inline Eval evalBsdf(const Bsdf& m, Vec3 wo, Vec3 wi, bool dropCosine = false, bool ndfPdf = false) {
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
