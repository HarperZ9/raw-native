#pragma once
// Development adjacency, the C++ core's float64 reference (ADR 0019): the Eberhard effect, Mackie
// lines and the small-area effect as one coupled diffusion system. The GPU passes are
// style_adj_prep, style_adj_develop, style_adj_chem and style_adj_out in
// src/renderer/gpu/shaders/style.wgsl; the JavaScript reference is web/shaders/adjacency/, whose
// header carries the model:
//   dD_k/dt = k c / (1 + beta b) (a_k - D_k)
//   dc/dt   = Dc lap(c) - eta sum_k dD_k/dt + r (1 - c)
//   db/dt   = Db lap(b) + etaB sum_k dD_k/dt - rB b
// with c and b on a grid s times coarser than the picture (read bilinearly), explicit Euler steps,
// reflecting borders, and the output colour times (D + eps) / (Dfree + eps).
#include <string>
#include <vector>
namespace raw::style {

struct AdjParams { double devUm{450}, bromUm{300}, k{3}, eta{0.6}, r{1.5}, beta{2}, etaB{0.5}, rB{1}, gamma{1.6}, H0{0.18}, exposure{1}, pitchUm{0}, T{1}; };
AdjParams adjPreset(const std::string& name);   // "eberhard", "mackie" or "exhausted"
struct AdjPlan {
    AdjParams p;
    int w{0}, h{0}, s{1}, cw{0}, ch{0}, steps{16};
    double Dc{0}, Db{0}, dt{0}, decay{0};
    double lapCentre{4};   // the 4 of the Laplacian; a source-edit control changes it in the code
};
// resolveAdjacency: the film pitch for a Super 35 width, the chemistry grid coarsened so the longer
// diffusion length spans about 2.5 cells.
AdjPlan resolveAdjacency(const AdjParams& p, int w, int h);
// scene: scene-linear RGBA f32 rows. Returns scene-linear RGBA, alpha 1.
std::vector<double> runAdjacency(const AdjPlan& plan, const std::vector<float>& scene);

}  // namespace raw::style
