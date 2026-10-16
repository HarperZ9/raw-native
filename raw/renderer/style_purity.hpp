#pragma once
// CRT colour purity and degauss, the C++ core's float64 reference (ADR 0019). The GPU pass is
// style_purity in src/renderer/gpu/shaders/style.wgsl; the JavaScript reference it is checked
// against is web/shaders/purity/purity.mjs, whose header carries the model and its sources.
//
// Per pixel, in triad-pitch units: phosphor stripes R, G, B at -1/3, 0, +1/3 of width s; gun k's
// beam is a footprint of width w centred at c_k + e_k, and the light of gun k on stripe j is the
// footprint's overlap with stripe j (and its neighbours a pitch away) over w. The landing error
//   e = magnetisation (two octaves of value noise) + earth (u r^2) + degauss,
//   degauss = A exp(-t_line / tau) sin(2 pi f t_line) (0.3 + r^2),  t_line = t + (y / H)(1 - blank) / field,
// and gun k lands with e (1 + spread (k - 1)). The degauss swing also shifts the picture by
// wobble (e_degauss / A) pixels, read with linear interpolation.
#include <string>
#include <vector>
namespace raw::style {

struct PurityPlan {
    double s{0.26}, spread{0.35}, w{0.2}, mag{0.16}, magScale{0.3}, earth{0}, degauss{0}, tau{0.25};
    double mains{60}, field{59.94}, blank{0.08}, wobble{0};
    double radial{0.3};   // the 0.3 in (0.3 + r^2); a source-edit control changes it in the code
};
// "magnetised", "earth-field" or "degauss" (PURITY_PRESETS); throws std::invalid_argument otherwise.
PurityPlan purityPreset(const std::string& name);
// src: display-linear RGBA f32 rows, w x h. Returns display-linear RGBA, alpha 1.
std::vector<double> runPurity(const PurityPlan& p, int w, int h, const std::vector<float>& src, double t);

}  // namespace raw::style
