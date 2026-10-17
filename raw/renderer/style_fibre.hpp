#pragma once
// Watercolour on a random fibre network, the C++ core's float64 reference (ADR 0019) of
// web/shaders/fibre/fibre.mjs, whose header carries the model: a random line process of fibres
// rasterised per pixel into coverage, an orientation tensor and a crossing density; a capillary
// conductivity tensor K = k0 I + k1 min(1, coverage) T_hat (smoothed over a 7 x 7 box); then a
// set number of explicit steps of pigment flux along a monotone eight-link stencil, deposition
// raised at fibre crossings, and linear evaporation. The wash starts wet in flat areas and dry
// across strong edges. Pigment is the colour as absorbance against the paper. The GPU passes are
// style_fibre_field, style_fibre_tensor, style_fibre_init, style_fibre_step and style_fibre_out
// in style.wgsl.
#include <array>
#include <string>
#include <vector>
namespace raw::style {

struct FibreParams {
    double edgeDry{6};
    int perCell{6};
    double len{0.05}, width{1.2}, md{0}, spread{1}, k0{0.25}, k1{0.9}, rate{0.08}, floc{1}, evap{0.02};
    int steps{40};
    double strength{1};
    std::array<double, 3> paper{0.96, 0.94, 0.89};
};
FibreParams fibrePreset(const std::string& name);   // cold-press, hot-press, rough
struct FibrePlan {
    FibreParams p;
    int w{0}, h{0};
    double L{0}, C{0}, dt{0};
    int tensorRadius{3};   // the 7 x 7 box's half width; a source-edit control changes it in the code
};
FibrePlan resolveFibre(const FibreParams& p, int w, int h);
// Per pixel: coverage, Txx, Txy, Tyy and the crossing density (5 values).
std::vector<double> fibrePaperField(const FibrePlan& plan);
// Per pixel: Kxx, Kxy, Kyy.
std::vector<double> fibreTensors(const FibrePlan& plan, const std::vector<double>& F);
// src: display-linear RGBA f32 rows. Returns display-linear RGBA, alpha 1.
std::vector<double> runFibre(const FibrePlan& plan, const std::vector<float>& src);

}  // namespace raw::style
