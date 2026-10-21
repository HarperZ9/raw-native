#pragma once
// Private to src/renderer: path tracer interface v3 (docs/rt-interface-v3.md). The environment's
// importance sampling, the full glTF material at a hit (evaluation and one-sample mixture
// sampling in its shading frame, float64), and the v3 path. A v2 scene never reaches this code.
#include "raw/renderer/rt_pathtrace.hpp"
#include "rt_pt_ctx.hpp"
#include <cstdint>
namespace raw::rt::v3 {

using pbr::D3;
inline D3 d3(Vec3 v) { return {double(v.x), double(v.y), double(v.z)}; }
inline Vec3 f3(D3 v) { return {float(v.x), float(v.y), float(v.z)}; }
inline Vec3 f3(lighting::Rgb c) { return {float(c.r), float(c.g), float(c.b)}; }

// The environment CDF and the tables (called by finishPtScene).
void finish(PtScene& s);
// Radiance along d, the solid-angle pdf of envSample choosing d, and a sample (false when none).
lighting::Rgb envRadiance(const PtScene& s, D3 d);
double envPdf(const PtScene& s, D3 d);
bool envSample(const PtScene& s, double u1, double u2, D3& dir, double& pdf);

// The material at one hit, in its shading frame (normal +z, tangent +x).
struct Shade {
    pbr::Material m;            // base colour already times the texel; thickness 0 for a traced volume
    D3 n, t, b;
    bool full{false};           // false: the v2 Lambert + GGX of rt_pt_math.hpp
    pt::Bsdf v2;
    double p[5]{};              // mixture probabilities: base VNDF, coat VNDF, cosine, transmission lobe, lower cosine
    double aBase{0}, aCoat{0}, eta{1};   // eta: index of refraction across the surface, as the ray meets it
};
Shade shadeAt(const PtScene& s, const ptc::Surf& x, const pbr::Material* full);
D3 toLocal(const Shade& h, D3 w);
D3 toWorld(const Shade& h, D3 w);
// f |cos| for local wo, wi; smooth: the Terms::smooth share (the diffuse AOV); pdf: the mixture's.
struct Eval { pbr::Rgb fcos, smooth; double pdf{0}; };
Eval eval(const PtScene& s, const Shade& h, D3 wo, D3 wi);
bool sample(const Shade& h, D3 wo, double pick, double u1, double u2, D3& wi);

// One v3 sample of a camera ray (the counterpart of rt_pt_cpu.cpp's tracePath).
ptc::Sample tracePath(const ptc::Ctx& C, const PtScene& s, const PathTraceDesc& d, Ray ray, std::uint32_t px, std::uint32_t smp);

}  // namespace raw::rt::v3
