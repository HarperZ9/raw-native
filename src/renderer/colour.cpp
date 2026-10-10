// Colour pipelines: a tone mapper followed by a display encoding (raw/renderer/colour.hpp).
#include "raw/renderer/colour.hpp"
#include "aces2.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::colour {
namespace {
struct Named { const char* name; Tone tone; Output out; float peak; };
const Named kPipelines[] = {
    {"clip/srgb", Tone::Clip, Output::Srgb, 100}, {"clip/display-p3", Tone::Clip, Output::DisplayP3, 100},
    {"clip/rec2020", Tone::Clip, Output::Rec2020, 100}, {"pbr-neutral/srgb", Tone::PbrNeutral, Output::Srgb, 100},
    {"pbr-neutral/display-p3", Tone::PbrNeutral, Output::DisplayP3, 100}, {"agx/srgb", Tone::AgX, Output::Srgb, 100},
    {"agx/display-p3", Tone::AgX, Output::DisplayP3, 100}, {"aces2-sdr/srgb", Tone::Aces2, Output::Srgb, 100},
    {"aces2-sdr/display-p3", Tone::Aces2, Output::DisplayP3, 100},
    {"aces2-hdr1000/rec2100-pq", Tone::Aces2, Output::Rec2100Pq, 1000},
    {"aces2-hdr1000/srgb-extended", Tone::Aces2, Output::SrgbExtended, 1000},
};

const Primaries& outputPrimaries(Output o){
    switch (o){
    case Output::DisplayP3: return kP3D65;
    case Output::Rec2020: case Output::Rec2100Pq: return kRec2020;
    default: return kRec709;
    }
}
// ACES 2.0 limits to the output gamut in SDR and to P3-D65 inside Rec.2020 for HDR,
// as the ACES 2.0 HDR P3-D65 output transforms do.
const Primaries& limitingPrimaries(Output o){ return o == Output::Rec2100Pq || o == Output::SrgbExtended ? kP3D65 : outputPrimaries(o); }

// OCIO receives the limiting primaries as float parameters.
Primaries asFloat(const Primaries& p){
    auto f = [](double v){ return (double)(float)v; };
    return {{f(p.r[0]), f(p.r[1])}, {f(p.g[0]), f(p.g[1])}, {f(p.b[0]), f(p.b[1])}, {f(p.w[0]), f(p.w[1])}};
}

double encode(Output o, double v){
    switch (o){
    case Output::Rec2020: return bt1886Encode(std::min(v, 1.0));
    case Output::Rec2100Pq: return pqEncode(v);
    case Output::SrgbExtended: return srgbEncodeExtended(v);
    default: return srgbEncode(std::min(v, 1.0));
    }
}

// OCIO's ACES 2.0 input stage: AP0 to AP1, clamp to [0, upper], back to AP0, in float.
aces2::f3 clampAp1(const aces2::f3& ap0, float peakNits){
    static const aces2::m33f toAp1 = aces2::to_f33(conversion(kAP0, kAP1, false));
    static const aces2::m33f toAp0 = aces2::to_f33(inverse(conversion(kAP0, kAP1, false)));
    const float upper = 8.f * (128.f + 768.f * (std::log(peakNits / 100.f) / std::log(10000.f / 100.f)));
    aces2::f3 v = aces2::mult_f3_f33(ap0, toAp1);
    for (float& x : v) x = std::clamp(x, 0.f, upper);
    return aces2::mult_f3_f33(v, toAp0);
}
}

bool parsePipeline(std::string_view name, Pipeline& out){
    for (const Named& n : kPipelines)
        if (name == n.name){ out = {n.tone, n.out, n.peak, n.name}; return true; }
    return false;
}

std::vector<std::string> pipelineNames(){
    std::vector<std::string> r;
    for (const Named& n : kPipelines) r.emplace_back(n.name);
    return r;
}

Transform::Transform(const Pipeline& p) : p_(p) {
    if (p.tone == Tone::Aces2){
        const Primaries lim = asFloat(limitingPrimaries(p.output));
        aces_ = std::make_unique<aces2::Transform>(p.peakNits, lim);
        outMatrix_ = conversion(lim, outputPrimaries(p.output), false);
    } else {
        outMatrix_ = conversion(kRec709, outputPrimaries(p.output), false);
    }
}
Transform::~Transform() = default;

RGB Transform::apply(const RGB& c) const {
    RGB lin;
    if (p_.tone == Tone::Aces2){
        static const aces2::m33f toAp0 = aces2::to_f33(conversion(kRec709, kAP0, true));
        const aces2::f3 ap0 = clampAp1(aces2::mult_f3_f33({c[0], c[1], c[2]}, toAp0), p_.peakNits);
        const aces2::f3 o = aces_->forward(ap0);
        const float top = p_.peakNits / 100.f;
        lin = {std::clamp(o[0], 0.f, top), std::clamp(o[1], 0.f, top), std::clamp(o[2], 0.f, top)};
    } else if (p_.tone == Tone::PbrNeutral) lin = pbrNeutral(c);
    else if (p_.tone == Tone::AgX) lin = agx(c);
    else lin = c;
    const RGB m = colour::apply(outMatrix_, lin);
    return {(float)encode(p_.output, m[0]), (float)encode(p_.output, m[1]), (float)encode(p_.output, m[2])};
}

std::vector<RGB> grid(){
    static const float axis[33] = {0.0f, 0.0009765625f, 0.0014281736221164465f, 0.0020886322017759085f, 0.003054519649595022f,
        0.004467081744223833f, 0.006532882805913687f, 0.00955401360988617f, 0.013972264714539051f, 0.020433735102415085f,
        0.029883312061429024f, 0.04370284453034401f, 0.06391321122646332f, 0.09346986562013626f, 0.1366949826478958f,
        0.19990955293178558f, 0.2923576831817627f, 0.42755845189094543f, 0.6252827644348145f, 0.914444625377655f,
        1.337329387664795f, 1.9557770490646362f, 2.8602256774902344f, 4.182936668395996f, 6.117334842681885f,
        8.946294784545898f, 13.083507537841797f, 19.13397216796875f, 27.982473373413086f, 40.9229621887207f,
        59.8477783203125f, 87.52437591552734f, 128.0f};
    std::vector<RGB> g;
    g.reserve(33 * 33 * 33 + 256 + 594);
    for (int b = 0; b < 33; ++b)
        for (int gg = 0; gg < 33; ++gg)
            for (int r = 0; r < 33; ++r) g.push_back({axis[r], axis[gg], axis[b]});
    for (int k = 0; k < 256; ++k){
        const float v = (float)std::exp2(-12.0 + 20.0 * k / 255.0);
        g.push_back({v, v, v});
    }
    const float hues[6][3] = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}};
    const float levels[3] = {0.18f, 1.0f, 8.0f};
    for (const auto& h : hues)
        for (float L : levels)
            for (int k = 0; k <= 32; ++k){
                const float t = k / 32.0f;
                g.push_back({L * ((1 - t) + t * h[0]), L * ((1 - t) + t * h[1]), L * ((1 - t) + t * h[2])});
            }
    return g;
}

}
