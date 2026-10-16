#pragma once
// Two style passes that relight a lit frame through the painter's pigments, the C++ core's float64
// references (ADR 0019). The GPU passes are style_lightpaint and style_glaze in
// src/renderer/gpu/shaders/style.wgsl; the JavaScript references they are checked against are
// web/shaders/lightpaint/lightpaint.mjs and web/shaders/glaze/glaze.mjs, whose headers carry the
// models. Both read a lit frame's albedo and irradiance; sky and emissive pixels pass through, and
// fog is mixed in after.
//
// Pigment-space lighting: e = Y(E), t = e / (e + Emid); shadow pigment is mixed in below t0 and
// light pigment above it (weights strength ((t0 - t) / t0)^shape and strength ((t - t0) / (1 -
// t0))^shape), Kubelka-Munk decodes the mix, and the result keeps the luminance of albedo * E.
// Interference glaze: a thin film (Airy, s and p averaged) over the pixel's own pigment body,
// combined by Kubelka's two-layer formula on 31 wavelengths; the film thickness is two octaves of
// 3D value noise on the surface's world position.
#include "raw/renderer/style_image.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

// A lit frame (web/shaders/fixtures/street.mjs outputs): RGBA f32 rows for albedo, irradiance and
// normals; per pixel fog fraction, kind (0 sky, 1 emissive, 2 lit) and view distance.
struct LitCamera { V3 eye{}, fwd{}, right{}, up{}; double tan{0.42}, aspect{1.6}; };
struct LitFrame {
    int w{0}, h{0};
    std::vector<float> albedo, light, normals, fogT, depth;
    std::vector<std::uint8_t> kind;
    V3 fogColor{};
    LitCamera camera;
};

struct LightpaintPlan {
    V4 shadow{}, light{};
    double Emid{0.35}, t0{0.5}, strength{0.5}, shape{1.5};
    bool complement{false};
    double lumG{0.7152};   // the G weight of Y; a source-edit control changes it in the code
};
// "disco", "grotesque" or "complement" (LP_PRESETS); throws std::invalid_argument otherwise.
LightpaintPlan lightpaintPreset(const std::string& name);
V3 lightpaintShade(const LightpaintPlan& p, const V3& albedo, const V3& E);
std::vector<double> runLightpaint(const LightpaintPlan& p, const LitFrame& f);   // RGBA, alpha 1

struct GlazePlan {
    double d0{330}, dVar{0}, noiseScale{0.4}, coverage{0.7}, n0{1.5}, n2{1.58}, A{2.2}, B{0.08};
    bool dispersion{true};
    double filmN{2.4};
};
GlazePlan glazePreset(const std::string& name);   // "beetle", "oil-slick" or "bruise"
double filmReflectance(const GlazePlan& p, double lambda, double cosAir, double d);
double glazeThickness(const GlazePlan& p, const V3& q);
V3 glazeColour(const GlazePlan& p, const V3& albedo, double cosAir, double d);
std::vector<double> runGlaze(const GlazePlan& p, const LitFrame& f);             // RGBA, alpha 1

}  // namespace raw::style
