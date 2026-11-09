#pragma once
// Private to the spectral caustic tracer: the packed tables and the per-path operations, written once
// for the CPU reference (rt_caustic_cpu.cpp). src/rhi/d3d12/hw_shaders/hw_caustic.hlsl mirrors every
// function here by name, in float32; a change to one is a change to the other.
//   Fresnel: the exact unpolarised dielectric reflectance (Born and Wolf, Principles of Optics, 1.5.2).
//   Sellmeier: refractiveindex.info dispersion formula 2, lambda in micrometres.
//   Illuminant A: CIE 15:2004 equation for S_A, Planck at 2848 K with c2 = 1.435e-2 m K.
//   CIE 1931 colour matching functions: src/renderer/data/cie1931.hpp, 5 nm, 380 to 780 nm.
#include "raw/renderer/rt_caustic.hpp"
#include "rt_pt_math.hpp"
#include <cmath>
#include <cstdint>
#include <vector>
namespace raw::rt::caustic {

// Table layout (floats): CMF x, y, z at 81 wavelengths, then illuminant A (81, 1 at 560 nm), then the
// materials, glasses and lights at the offsets in the parameters.
inline constexpr std::uint32_t kCmf = 0, kIllum = 243, kTablesHead = 324;
inline constexpr std::uint32_t kMatFloats = 4, kGlassFloats = 8, kLightFloats = 16;
inline constexpr float kLambda0 = 380.0f, kLambdaSpan = 400.0f, kPi = 3.14159265f;
inline constexpr std::uint32_t kLambdaDim = 63u;   // the wavelength's dimension, apart from the path's
inline constexpr std::uint32_t F_SPLIT = 1u, F_NODISP = 2u, F_DROPCOS = 4u, F_FIRST = 8u;

// The tracer's random numbers: a nested integer hash of (seed, pixel, sample, dimension), lowbias32
// by C. Wellons (2018). The path tracer's PCG2D over (pixel, sample * 64 + dimension) was used first;
// with it the two estimators of K2 disagreed by 1.5 to 3% and the disagreement moved when only the
// wavelength's dimension moved (evidence/hw-c1-caustic-runs.json, runs 2 and 3).
inline std::uint32_t lowbias32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline pt::R2 rand2(std::uint64_t seed, std::uint32_t pixel, std::uint32_t sample, std::uint32_t dim) {
    const std::uint32_t h = lowbias32(pixel ^ lowbias32(sample ^ lowbias32(dim ^ std::uint32_t(seed) ^ lowbias32(std::uint32_t(seed >> 32)))));
    const std::uint32_t g = lowbias32(h ^ 0x9E3779B9u);
    return {float(h >> 8) * 5.96046448e-8f, float(g >> 8) * 5.96046448e-8f};
}
struct Packed {
    std::vector<float> tables, geo;   // geo: 12 floats a triangle: three shading normals, material, 2 pad
    std::uint32_t matOff{0}, glassOff{0}, lightOff{0}, lights{0};
};
Packed pack(const CausticScene& s);

// Linear interpolation in a 5 nm table of 81 entries with `stride` floats per entry.
inline float tableAt(const float* t, std::uint32_t base, std::uint32_t stride, std::uint32_t k, float lambda) {
    const float x = std::fmin(std::fmax((lambda - kLambda0) / 5.0f, 0.0f), 80.0f);
    const std::uint32_t i = std::uint32_t(std::fmin(std::floor(x), 79.0f));
    const float f = x - float(i);
    return t[base + i * stride + k] * (1.0f - f) + t[base + (i + 1) * stride + k] * f;
}
inline float sellmeier(const float* g, float lambdaNm) {
    const float l2 = (lambdaNm * 1e-3f) * (lambdaNm * 1e-3f);
    const float n2 = 1.0f + g[0] * l2 / (l2 - g[3]) + g[1] * l2 / (l2 - g[4]) + g[2] * l2 / (l2 - g[5]);
    return std::sqrt(n2);
}
inline float indexAt(const float* tables, std::uint32_t glassOff, std::uint32_t glass, float lambda, std::uint32_t flags, float offset) {
    const float* g = tables + glassOff + glass * kGlassFloats;
    return ((flags & F_NODISP) != 0u ? g[6] : sellmeier(g, lambda)) + offset;
}
// Reflectance for a ray from a medium into one of relative index eta = n_t / n_i, cosI > 0.
inline float fresnel(float cosI, float eta) {
    const float s2 = (1.0f - cosI * cosI) / (eta * eta);
    if (s2 >= 1.0f) return 1.0f;
    const float cosT = std::sqrt(1.0f - s2);
    const float rs = (cosI - eta * cosT) / (cosI + eta * cosT);
    const float rp = (eta * cosI - cosT) / (eta * cosI + cosT);
    return 0.5f * (rs * rs + rp * rp);
}
// The pt tracer's ray offset: 2e-4 (1 + max |p|) along ng, to the side of dir.
inline Vec3 offsetP(Vec3 p, Vec3 ng, Vec3 dir) {
    const float e = 2e-4f * (1.0f + std::fmax(std::fabs(p.x), std::fmax(std::fabs(p.y), std::fabs(p.z))));
    return p + ng * (dot(ng, dir) > 0.0f ? e : -e);
}
// One glass interaction. d the incoming direction, ns the interpolated shading normal, ng the
// geometric normal (both outward), n the index at this wavelength, u in [0, 1). Returns the new
// direction; the throughput is unchanged (reflect with probability F, else refract).
inline Vec3 glassStep(Vec3 d, Vec3 ns, Vec3 ng, float n, float u) {
    const bool entering = dot(ng, d) < 0.0f;
    const float eta = entering ? n : 1.0f / n;
    Vec3 m = dot(ns, d) < 0.0f ? ns : ns * -1.0f;
    float cosI = -dot(m, d);
    if (!(cosI > 0.0f)) { m = dot(ng, d) < 0.0f ? ng : ng * -1.0f; cosI = -dot(m, d); }
    const float F = fresnel(cosI, eta);
    if (u < F) return normalize(d + m * (2.0f * cosI));
    const float cosT = std::sqrt(std::fmax(0.0f, 1.0f - (1.0f - cosI * cosI) / (eta * eta)));
    return normalize(d * (1.0f / eta) + m * (cosI / eta - cosT));
}
// A cosine-weighted direction about n from two uniforms.
inline Vec3 cosineDir(Vec3 n, float u1, float u2) {
    const pt::Frame f = pt::basis(n);
    const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
    const float z = std::sqrt(std::fmax(0.0f, 1.0f - u1));
    return normalize(f.t * (r * std::cos(phi)) + f.b * (r * std::sin(phi)) + n * z);
}
struct Cam { Vec3 eye, fwd, side, up; float tanHalf, aspect; };
inline Cam camOf(const CausticCamera& c, int w, int h) {
    const Vec3 f = normalize(c.target - c.eye), s = normalize(cross(f, c.up)), u = cross(s, f);
    return {c.eye, f, s, u, std::tan(c.fovy * 0.5f), float(w) / float(h)};
}
inline Vec3 camDir(const Cam& c, float px, float py, int w, int h) {
    const float nx = px / float(w) * 2.0f - 1.0f, ny = 1.0f - py / float(h) * 2.0f;
    return normalize(c.fwd + c.side * (nx * c.tanHalf * c.aspect) + c.up * (ny * c.tanHalf));
}
// The illuminant A spectrum, relative: S_A(lambda) / S_A(560).
double illuminantA(double lambdaNm);

}  // namespace raw::rt::caustic
