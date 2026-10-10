// Packing and reporting for the lighting's GPU parity: see raw/renderer/lighting_parity.hpp.
#include "raw/renderer/lighting_parity.hpp"
#include "raw/renderer/pbr_parity.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::gpu_check {
namespace {
using lighting::D3;
using lighting::Rgb;
float f32(double v) { return float(v); }
void put3(float* o, D3& c) { o[0] = f32(c.x); o[1] = f32(c.y); o[2] = f32(c.z); c = {o[0], o[1], o[2]}; }
}  // namespace

std::vector<float> packLights(const std::vector<lighting::Light>& ls) {
    std::vector<float> v(ls.size() * kLightFloats, 0.0f);
    for (std::size_t k = 0; k < ls.size(); ++k) {
        const lighting::Light& l = ls[k];
        float* o = v.data() + k * kLightFloats;
        o[0] = l.type == lighting::LightType::Directional ? 0.0f : l.type == lighting::LightType::Point ? 1.0f : 2.0f;
        o[1] = f32(l.position.x); o[2] = f32(l.position.y); o[3] = f32(l.position.z);
        o[4] = f32(l.direction.x); o[5] = f32(l.direction.y); o[6] = f32(l.direction.z);
        o[7] = f32(l.color.r); o[8] = f32(l.color.g); o[9] = f32(l.color.b);
        o[10] = f32(l.intensity); o[11] = f32(l.range);
        const double scale = 1.0 / std::max(0.001, std::cos(l.innerCone) - std::cos(l.outerCone));
        o[12] = f32(scale); o[13] = f32(-std::cos(l.outerCone) * scale);
    }
    return v;
}

void packSample(lighting::Sample& s, float* out) {
    PbrCase c{s.m, {0, 0, 1}, {0, 0, 1}, 0};
    float tmp[kPbrCaseFloats];
    packCase(c, tmp);                       // the material fields, rounded in place in c.m
    s.m = c.m;
    std::copy(tmp, tmp + 36, out);
    put3(out + 36, s.p); put3(out + 39, s.n); put3(out + 42, s.t); put3(out + 45, s.v);
}

std::vector<float> packEnvironment(const lighting::Cube& c, const std::vector<Rgb>& sh) {
    std::vector<float> v;
    v.reserve(c.rgb.size() + 27);
    for (double x : c.rgb) v.push_back(f32(x));
    for (const Rgb& k : sh) { v.push_back(f32(k.r)); v.push_back(f32(k.g)); v.push_back(f32(k.b)); }
    return v;
}

std::string LightingParity::json() const {
    char b[2048];
    std::snprintf(b, sizeof b,
        "{\n \"check\": \"m3 lighting gpu parity\",\n \"backend\": \"%s\",\n \"adapter\": \"%s\",\n \"error\": \"%s\",\n"
        " \"seed\": %llu,\n \"ev100\": %.1f,\n"
        " \"clusters\": {\"scenes\": 8, \"lights_each\": 256, \"differences_off_boundary\": %ld, \"boundary_differences\": %ld,"
        " \"points\": %ld, \"gpu_list_misses\": %ld},\n"
        " \"prefilter\": {\"bound\": \"|gpu - ref| <= 1e-3 |ref| + 1e-6\", \"worst_ratio_to_bound\": %.4f,"
        " \"control_half_samples_ratio\": %.3f},\n"
        " \"shading\": {\"samples\": %u, \"bound\": \"|gpu - ref| <= 2e-3 |ref| + 1e-5 (exposed)\", \"worst_ratio_to_bound\": %.4f,"
        " \"worst_abs\": %.3e, \"worst_sample\": %d, \"values_outside\": %d, \"control_lights_dropped_ratio\": %.3f},\n"
        " \"pass\": %s\n}\n",
        backend.c_str(), adapter.c_str(), error.c_str(), (unsigned long long)kLightingSeed, kLightingEv100, clusterDiffs,
        clusterBoundaryDiffs, missPoints, gpuMisses, prefilterWorstRatio, prefilterControlRatio, samples, shadeWorstRatio,
        shadeWorstAbs, shadeWorstSample, shadeOutside, shadeControlRatio, pass() ? "true" : "false");
    return b;
}

}  // namespace raw::gpu_check
