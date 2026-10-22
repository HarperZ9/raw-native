#pragma once
// Dither-quantised lighting, the C++ core's float64 reference (ADR 0019). Light and shadow are
// resolved through a threshold matrix into N tones, with 1 bit (N = 2) as the limit case. The GPU
// pass is style_ditherlight in src/renderer/gpu/shaders/style.wgsl; the JavaScript reference it
// is checked against is web/shaders/ditherlight/.
//
//   lit  = albedo * (d (1 - h + h (0.5 + 0.5 n.y)) + sum_i vis_i mask_i term_i)
//   tone = clamp(lit, 0, 1) (1 - tf) + tf,   tf = fogMax (1 - exp(-(depth / fogDist)^2))
// Sky (kind 0) and emissive (kind 1) pixels are already tones and skip the lights and the fog.
// A per-pixel haze h in [0, 1] (light scattered along the view ray) screen-blends every kind:
// tone = 1 - (1 - tone)(1 - h).
// Levels sit at linear light l_k = (k / (N - 1))^gamma; a tone in [l_k, l_k+1] takes level k + 1
// when its fraction f = (Y - l_k) / (l_k+1 - l_k) exceeds the pixel's threshold, else level k.
// A threshold is (rank + 0.5) / M for a Bayer 8 x 8 rank (M = 64) or a blue-noise rank (M = 4096).
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

using V3 = std::array<double, 3>;

enum class Source : std::uint32_t { Bayer8 = 0, Blue64 = 1 };
enum class Anchor : std::uint32_t { Screen = 0, WorldFixed = 1, WorldOctave = 2, Fractal = 3 };

struct DitherPlan {
    int levels{2};
    V3 ink{0.012, 0.011, 0.01}, paper{0.92, 0.9, 0.84};   // linear light
    Source source{Source::Bayer8};
    Anchor anchor{Anchor::Screen};
    double cell{0.1};      // world size of a cell (world anchors)
    double px{2};          // pixels a cell should cover (world-octave)
    double pa{0};          // world size of one pixel at unit depth (pixelAngle)
    double gamma{2.2};
    double dark{0.16}, hemi{0.5};
    double fogDist{38}, fogMax{1};
    double fractalDepth{0};  // Fractal: > 0 fixes the depth in the scale term (a control only, breaks scale invariance)
    double hemiConst{0.5};   // the 0.5 in (0.5 + 0.5 n.y); a control changes it, nothing else does
};
// World size of one pixel at unit depth, for a vertical field of view in radians.
double pixelAngle(double fov, int height);
// The presets "ink" (2 levels) and "tones4" (4 levels), sized for a frame `height` pixels high.
// Throws std::invalid_argument for an unknown preset.
DitherPlan ditherPreset(const std::string& name, int height, double fov);

struct DitherLight {
    enum class Kind : std::uint32_t { Radial = 0, Beam = 1 } kind{Kind::Radial};
    V3 pos{0, 0, 0};
    double intensity{1}, wrap{0.3};
    double radius{7}, core{1.5};                         // radial
    V3 dir{0, 0, -1}, tan{1, 0, 0}, bit{0, 1, 0};        // beam: unit axis and its basis
    double outer{0}, inner{0}, range{40};                // beam: cosines of the half-angles
    double slats{0}, duty{0.5}, phase{0};
};
DitherLight makeRadial(V3 pos, double radius = 7, double core = 1.5, double intensity = 1, double wrap = 0.3);
DitherLight makeBeam(V3 pos, V3 dir, double outerDeg = 9, double innerDeg = 4, double range = 40, double intensity = 1.6,
                     double slats = 0, double duty = 0.5, double phase = 0, double wrap = 0.1);

int bayer8Rank(int x, int y);
int blueRank(int x, int y);
double thresholdAt(Source s, int cx, int cy);
int octaveOf(double cell, double px, double pa, double depth);
double maskAt(const DitherLight& L, const V3& P);
double surfaceTerm(const DitherLight& L, const V3& P, const V3& N);
double levelLinear(const DitherPlan& p, int k);
std::array<std::uint8_t, 3> levelRgb8(const DitherPlan& p, int k);

// One G-buffer pixel, as the geometry pass writes it.
struct DitherPixel {
    int kind{2};                 // 0 sky, 1 emissive, 2 lit surface
    double albedo{0}, depth{0}, haze{0};
    V3 wpos{}, nrm{0, 0, 1}, apos{}, anrm{0, 0, 1};
    std::array<double, 4> vis{};
    int ix{0}, iy{0};
};
double toneOf(const DitherPlan& p, const std::vector<DitherLight>& lights, const DitherPixel& px);
std::array<int, 2> anchorCell(const DitherPlan& p, const DitherPixel& px);
// Tone to a level for threshold t. `fraction`, when given, receives f (for tie reports).
int quantise(const DitherPlan& p, double Y, double t, double* fraction = nullptr);

// Surface-stable fractal dithering (Anchor::Fractal; evidence/fractal-dither-bounds.json). The
// threshold of a pixel is a Bayer hierarchy over the anchor chart at a continuous scale: with
// lambda = log2(px pa depth / cell), the fine level cell * 2^floor(lambda) and the coarse level
// twice that are blended with weight m = 1 - frac(lambda) and the blend is mapped through its own
// CDF, so the threshold stays uniform, equals the coarse pattern at m = 0 and the fine one at m = 1.
// Idea after Rune Skovbo Johansen's Dither3D (MPL-2.0); written from the published description, no code taken.
inline constexpr int kFractalDigits = 6;                 // rank_K: a 64 x 64 tile of 4096 ranks
int fractalRank(int x, int y, int digits = kFractalDigits);   // digits = 3 is bayer8Rank
double fractalBlend(double tc, double tf, double m);          // the CDF-mapped blend, m in [0, 1]
struct FractalCell { double lambda{0}, m{0}; int i{0}, j{0}; };
FractalCell fractalCell(const DitherPlan& p, const DitherPixel& px, int cellShift = 0);
double fractalThreshold(const DitherPlan& p, const DitherPixel& px, int cellShift = 0);

// A whole G-buffer (f32 storage, as a GPU pass reads it): 3 floats a pixel for the vectors,
// 4 for vis.
struct DitherGBuffer {
    int w{0}, h{0};
    std::vector<float> albedo, depth, wpos, nrm, apos, anrm, vis;
    std::vector<float> haze;   // one a pixel, or empty for none
    std::vector<std::uint8_t> kind;
    DitherPixel pixel(int x, int y) const;
};
struct DitherFrame {
    int w{0}, h{0};
    std::vector<std::uint8_t> level, rgba;   // a level and RGBA8 a pixel
    std::vector<double> tone, fraction, threshold;
};
// `cellShift` adds to the threshold cell (a control only).
DitherFrame runDitherlight(const DitherPlan& p, const std::vector<DitherLight>& lights, const DitherGBuffer& g, int cellShift = 0);

}  // namespace raw::style
