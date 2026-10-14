#pragma once
// Checks on the path tracer's v2 split radiance (P5 of evidence/rt-r2-bounds.json, addendum 5),
// shared by the CPU test and the GPU parity run. Each returns a JSON object for the evidence.
#include "raw/renderer/rt_pathtrace.hpp"
#include <string>
namespace raw::rt {

// P5a: |diffuse + specular + emission - radiance| <= 1e-4 (1 + |radiance|) on every value.
struct SplitSum {
    long values{0}, over{0}, nonfinite{0};
    double worstRatio{0};   // worst |sum - r| / (1e-4 (1 + |r|))
    bool within() const { return values > 0 && over == 0 && nonfinite == 0; }
};
SplitSum splitSum(const PathTraceOutput& o);
std::string splitSumJson(const SplitSum& s, const char* scene, const char* side, bool control);

// P5b: the pure-Lambert furnace (emission E, albedo a on every face, every primary ray a hit):
// specular exactly 0, emission exactly E, diffuse within 4 standard errors of E a / (1 - a),
// every pixel within 6 of its own.
struct FurnaceSplit {
    long specularNonzero{0}, emissionWrong{0};
    double meanDiffuse{0}, expected{0}, z{0}, worstPixelZ{0};
    bool within() const { return specularNonzero == 0 && emissionWrong == 0 && z <= 4.0 && worstPixelZ <= 6.0; }
};
FurnaceSplit furnaceSplit(const PathTraceOutput& o, float E, float a);
std::string furnaceSplitJson(const FurnaceSplit& f, float a, const char* side, bool control);

// P5c: one component (diffuse or specular) of two independent renders, by P2's statistic on
// its luminance and its own variance: mean |z| <= 0.9, fraction z > 4 <= 0.002, image z <= 3.
struct SplitConv {
    double meanAbsZ{0}, fracOver4{0}, imageZ{0};
    long nonfinite{0};
    bool within() const { return nonfinite == 0 && meanAbsZ <= 0.9 && fracOver4 <= 0.002 && imageZ <= 3.0; }
};
SplitConv splitConv(const PathTraceOutput& g, const PathTraceOutput& c, bool specular);
std::string splitConvJson(const SplitConv& v, const char* scene, bool specular, bool control);

}  // namespace raw::rt
