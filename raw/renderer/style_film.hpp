#pragma once
// The spectral film shader, the C++ core's reference (ADR 0019) of web/shaders/film (film.mjs,
// stocks.mjs, grain.mjs, run.mjs) and the faceplate optics it borrows (crt/halo.mjs, glass.mjs).
// Scene-linear Rec.709 in, display-linear Rec.709 out (1 = the projected open gate). Passes:
//   expose    gate weave, then the camera negative's layer exposures (spectral, 3 x 3)
//   mtf       the emulsion's own scatter, a separable gaussian
//   halo      light that reaches the film base and comes back (a kernel derived from the optics)
//   develop   characteristic curves and interimage -> each layer's developed dye fraction
//   print     Boolean-model grain on the negative, then the full spectral print and projection
// The JavaScript stores each pass's output (and the MTF and halo weights and the J table) as f32
// and computes in float64 between; so does this. Stocks are parametric approximations of
// data-sheet shapes (docs/shaders/CONSTANTS.md). The GPU passes are style_film_* in style.wgsl.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

using M3 = std::array<double, 9>;
using T3 = std::array<double, 3>;

// The tables and constants of a negative / print pair (stocks.mjs build), all at 41 wavelengths
// (380 to 780 nm in 10 nm steps).
struct FilmStock {
    M3 E{};                                    // layer exposure from scene RGB, rows sum to 1
    std::vector<T3> negPrim, printW, printPrim, projV;
    std::vector<double> negConst;
    double printBase{0}, bleach{0}, interimage{0};
    T3 sigmaD48{};                             // RMS granularity per layer at 48 um, net density 1
    T3 negDmin{}, negSpan{}, negK{}, negX0{}, prtDmin{}, prtSpan{}, prtK{}, logGain{}, grainUm{};
    M3 out{};                                  // projected XYZ to display Rec.709, white adapted
};
struct FilmParams {
    std::string negative{"neg-500t"}, print{"print-2383"}, process{"normal"};
    T3 printerPoints{0, 0, 0};
    double ev{0}, gateWidthMm{24.89}, grainScale{1}, grainAmount{1};
    bool remjet{true};
    double pressurePlate{0.05};
    T3 emulsionSigmaUm{3.33, 3.75, 5.54};   // per layer: 50% MTF at 56.3, 49.9, 33.8 c/mm (H-1-5219t)
    T3 halationReach{0.25, 0.06, 0};
    double weaveUm{6}, jitterUm{1.5};
    int seed{1};
    double interimageScale{1};   // 1; a source-edit control changes the interimage gain in the code
};
FilmParams filmPreset(const std::string& name);   // 500t-print, 500t-no-remjet, 250d-print, bleach-bypass
FilmStock buildFilmStock(const FilmParams& p);

struct FilmPlan {
    FilmParams p;
    FilmStock t;
    int inW{0}, inH{0}, outW{0}, outH{0};
    double umPerPx{0};
    std::vector<float> jTable;                 // the Boolean model's variance integral, 33 entries
    T3 grainContrast{}, haloStrength{}, grainUm{};
    int haloQ{1}, haloR{0};
    double ringMm{0};
    std::vector<float> haloWeights;            // (2R+1)^2, normalised
    std::vector<float> mtf;                    // separable emulsion blur weights per layer, interleaved (3 entries: none)
};
FilmPlan resolveFilm(const FilmParams& p, int inW, int inH, int outW, int outH);

// The faceplate halo kernel (crt/halo.mjs haloKernel) and the grain tables (grain.mjs).
struct HaloKernel { int q{1}, R{0}; std::vector<float> weights; double ringMm{0}, kappa{0}; };
struct BaseGlass { double thickness, n, transmission, albedo, cell, backReflectance; };
HaloKernel filmHaloKernel(const BaseGlass& g, double mmPerPx, int maxCells);
double filmRadialKappa(const BaseGlass& g, double dr, double rMax);
std::vector<float> filmVarianceTable();
const std::vector<std::uint32_t>& filmPoissonTable();   // 512 levels x 16 thresholds
std::vector<float> filmMtfWeights(const T3& sigmaPx);   // interleaved w[3 (k + r) + c]; {1, 1, 1}: none
// Gate weave of frame f in output pixels.
std::array<double, 2> filmWeave(const FilmPlan& plan, int f);
// Grain on layer c at pixel (px, py): the developed fraction after grain (grainAt). F is the
// developed-fraction frame (RGBA f32, output size).
double filmGrainAt(const FilmPlan& plan, int px, int py, int c, const std::vector<float>& F, int frame);

// The per-pixel spectral path (stocks.mjs): negative densities and the printed, projected colour.
T3 filmInterimage(const FilmStock& t, const T3& D);
T3 filmPrintPixel(const FilmStock& t, const T3& D);

// One frame: scene is RGBA f32 rows (inW x inH); returns the display-linear image, RGBA f32 rows.
std::vector<float> filmFrame(const FilmPlan& plan, const std::vector<float>& scene, int f);
// encodeFilm8: clamp, sRGB, round half up.
std::uint8_t filmEncode8(double v);

}  // namespace raw::style
