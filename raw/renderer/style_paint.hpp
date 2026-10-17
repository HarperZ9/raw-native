#pragma once
// The painterly shader, the C++ core's reference (ADR 0019) of web/shaders/paint (paint.mjs,
// abstract.mjs, compose.mjs, canvas.mjs, noise.mjs). Scene light becomes a display colour and its
// Kubelka-Munk pigment latent; a smoothed structure tensor gives flow and anisotropy; the
// anisotropic Kuwahara filter (Kyprianidis, Kang, Doellner) averages pigment latents, so it mixes
// paint; brush relief and stroke identity integrate noise along the flow; relief adds XDoG lines
// and lit impasto; compose pushes temperature in pigment space, breaks colour per stroke, masses
// values and, for watercolour, lays a Kubelka-Munk glaze over paper. Noise is read at canvas
// coordinates: the pixel plus a pan offset, or two advected layers under a moving camera (after
// Neyret, "Advected Textures"). The JavaScript stores every pass's output as f32; so does this.
// The GPU passes are style_paint_* and style_canvas_* in style.wgsl.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct PaintParams {
    int medium{0};   // 0 oil, 1 matte, 2 water
    double radius{6}, q{8}, zeta{0.33}, strokeLen{10}, impasto{0.6}, gloss{0.4}, push{0.12}, broken{0.15}, valueBands{0};
    double lines{0.2}, lineSigma{1.0}, lineSharp{30}, warp{0}, exposure{2.2}, granulation{0}, edge{0}, dilution{1};
    std::array<double, 2> canvasOffset{0, 0};
    double tensorSigma{2};
    double xdogWeight{0.985};   // a source-edit control changes it in the code
};
PaintParams paintPreset(const std::string& name);   // oil-grotesque, oil, gouache, watercolour
struct PaintPlan { PaintParams p; int w{0}, h{0}; std::vector<float> tw; };
PaintPlan resolvePaint(const PaintParams& p, int w, int h);
std::vector<float> paintGaussWeights(double sigma);

// The noise fields, at canvas coordinates.
double paintVnoise(double x, double y, std::uint32_t seed);
enum class PaintNoise { Bristle, StrokeId, Canvas, Paper, PaperHalf, WarpX, WarpY };
double paintNoise(PaintNoise kind, double x, double y);

// The advected canvas (canvas.mjs): two layers of canvas coordinates carried along motion vectors.
struct PaintLayer { double age{-1}; std::array<double, 2> seed{0, 0}; double meanD{0}; bool reset{false}; };
struct PaintCanvas {
    int w{0}, h{0};
    std::vector<float> C, prevDist;
    int frame{0};
    std::array<PaintLayer, 2> layer;
    std::array<double, 2> statSum{0, 0}, statN{0, 0};
    PaintCanvas(int w_, int h_);
};
void paintDecide(PaintCanvas& st);
void paintAdvect(PaintCanvas& st, const std::vector<float>& mv, const std::vector<float>& dist, const std::vector<float>& distPrev);
std::array<double, 2> paintLayerWeights(const PaintCanvas& st);

// One frame: scene is scene-linear RGBA f32 rows; canvas null for the offset canvas. Returns the
// display-linear image, RGBA f32 rows.
std::vector<float> runPaint(const PaintPlan& plan, const std::vector<float>& scene, const PaintCanvas* canvas);
std::uint8_t paintEncode8(double v);

}  // namespace raw::style
