#pragma once
// P23, the tilt-shift lens (evidence/tiltshift-bounds.json): a thin lens whose plane of sharp focus
// is tilted in the world (Scheimpflug; the hinge rule after Merklinger, "Focusing the View Camera",
// 1993), circles of confusion per pixel from depth against that plane, defocus painted as a coarser
// anisotropic Kuwahara over Kubelka-Munk pigment latents (the ink falls off with it), and an
// optional lens and print effect stack. Float64 reference; the GPU passes are art_ts_* in
// src/renderer/gpu/shaders/art_tiltshift.wgsl and mirror this arithmetic.
#include "raw/renderer/art_scene.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::art {

// The lens. The plane of focus passes through `focus`, contains the horizontal `axis` (zero: the
// camera's right vector) and stands `tiltDeg` from the ground (90 is upright). The scene is seen as
// a model at 1:K; unitMm is millimetres a world unit at 1:1.
struct Lens {
    D3 focus{0, 0, 0}, axis{0, 0, 0};
    double tiltDeg{90}, rollDeg{0}, K{1}, fNumber{2.8}, unitMm{1000}, cMax1080{48};
};
struct LensGeom {
    D3 eye{0, 0, 0}, fwd{0, 0, 1}, n{0, 0, 1};
    double d{0}, fpx{1}, fmm{1}, mmPerUnit{1}, N{2.8}, cMax{16};
};
LensGeom resolveLens(const Lens& l, const View& v);
// The CoC diameter in pixels along view ray `dir` at distance `dist` (negative: sky, at infinity).
double cocAt(const LensGeom& g, const D3& dir, double dist);
double cocAtPoint(const LensGeom& g, const D3& p);
// The equivalent lens tilt by the hinge rule, in degrees (0 for a plane parallel to the sensor).
double hingeTiltDeg(const LensGeom& g);
// Two floats a pixel: the CoC in pixels and the axial depth in world units (1e6 for sky).
std::vector<float> cocMap(const Frame& f, const LensGeom& g);
// The screen-band control: c = cMax * clamp((|y - yF| - half) / ramp, 0, 1), ramp 0.15 of the
// height, centred on the focus point's row, half chosen so as many pixels have c <= c0 as in
// `lens`; depth copied from `lens`.
std::vector<float> bandCoc(const std::vector<float>& lens, const LensGeom& g, const View& v, const D3& focus, double c0);

// The painted defocus.
struct DefocusParams { double c0{1}, c1{2.5}, q{8}, zeta{0.33}, rim{2}; };
inline constexpr int kTsSamples = 32;
// The fixed 48-sample Vogel disc (x, y) and each sample's eight sector weights, as f32: the GPU
// reads the same table, so both sides weigh identical numbers.
struct DefocusTables { std::vector<float> pos, wk; };
const DefocusTables& defocusTables();
// One float a pixel: smoothstep(c0, c1, c) times the rim guard (0 within `rim` px of a nearer
// in-focus pixel).
std::vector<float> defocusWeight(const std::vector<float>& coc, int w, int h, const DefocusParams& p);
enum class DefocusMode { Paint, Gaussian };
// The half-resolution input (addendum C): the 2 x 2 mean of the unlined colour (rgb / max(alpha,
// 0.05), alpha being the paint frame's line factor) and of the CoC and depth.
std::vector<float> unlinedHalf(const std::vector<float>& img, const std::vector<float>& coc, int w, int h, std::vector<float>& hcoc);
// img: display-linear RGBA f32, alpha the line factor. The Kuwahara (or gaussian) runs at half
// resolution on the unlined colour; the full-resolution compose mixes it in by the weight, so the
// ink fades as (1 - w). Returns display-linear RGBA f32.
std::vector<float> paintDefocus(const std::vector<float>& img, const std::vector<float>& coc, int w, int h, const DefocusParams& p,
                                DefocusMode mode, int threads);

// A look: the lens settings and every effect's strength (0 is off). Looks lerp for morphs.
struct TiltLook {
    std::string name;
    double K{1}, fNumber{2.8}, tiltDeg{90}, rollDeg{0};
    bool streetAxis{false};
    double vignette{0}, mech{0}, halation{0}, star{0}, starRotDeg{0}, ca{0}, grain{0}, weave{0}, print{0}, misreg{0}, halftone{0};
    double sat{1}, hueDeg{0};   // saturation toward luminance and a hue rotation (the dream, 01-LOOKS L6)
};
TiltLook tiltLook(const std::string& name);   // daily, town_map, record, memory, dream, night
TiltLook lerpLook(const TiltLook& a, const TiltLook& b, double t);
Lens lensOf(const TiltLook& look, const D3& focus, const D3& streetAxis);

struct FxFrame { int frame{0}; bool reducedMotion{false}, reduceFlashes{false}; };
struct FxPlan {
    int w{0}, h{0}, qw{0}, qh{0};
    double fpx{1}, weaveX{0}, weaveY{0}, haloR{1}, starStep{1}, thr{0.7}, knee{0.25}, cell{6};
    std::uint32_t seed{7};
    std::array<double, 8> dirs{};   // four star directions (cos, sin)
    TiltLook look;
    FxFrame fr;
};
FxPlan planFx(const TiltLook& look, const FxFrame& fr, int w, int h, double fpx);
inline constexpr int kHaloTaps = 24;
const std::vector<float>& haloTable();   // 24 (x, y) on the unit disc, f32
// Quarter-resolution warm bright pass (RGBA a quarter pixel), the glow (halo RGBA then star RGBA a
// quarter pixel) and the full-resolution finish (display-linear RGBA).
std::vector<float> fxBright(const FxPlan& P, const std::vector<float>& img);
std::vector<float> fxGlow(const FxPlan& P, const std::vector<float>& bright);
std::vector<float> fxFinish(const FxPlan& P, const std::vector<float>& img, const std::vector<float>& glow);
std::vector<float> fxStack(const FxPlan& P, const std::vector<float>& img);

}  // namespace raw::art
