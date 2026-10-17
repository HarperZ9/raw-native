#pragma once
// The physical CRT, the C++ core's reference (ADR 0019) of web/shaders/crt (params.mjs,
// phosphors.mjs, halo.mjs, signal.mjs, beam.mjs, geometry.mjs, glass.mjs, layout.mjs). A plan in
// physical units (millimetres on the tube face, signal samples at 4 fsc, the phosphor screen's
// gun-to-output matrix and decay table, the faceplate halo kernel derived from the glass optics);
// the video signal through RGB, S-video or composite with a notch or comb decoder; the beams'
// gaussian spots integrated over each output pixel's footprint on the curved face, the mask and
// the phosphor persistence; then the halo, the phosphor colour, the room reflected in the glass,
// and the display encode. The JavaScript stores each pass's output as f32 and computes in
// float64 between; so does this. The GPU passes are style_crtp_* in style.wgsl.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct CrtParams {
    int mode{0};                        // signal: 0 rgb, 1 s-video, 2 composite
    bool pal{false}, comb{true};        // standard; decoder comb (true) or notch
    double pixelClockMHz{5.369318}, encodeChromaMHz{1.3}, rgbMHz{8};
    double diagonalIn{20}, aspect{4.0 / 3.0}, overscan{0}, corner{12}, gamma{2.4}, contrast{1}, brightness{0};
    double zoom{1}, centre[2]{0, 0}, border{0.08};
    double rx{1500}, ry{1e9};
    double spotLo{0.16}, spotHi{0.34}, spotExp{0.5};
    int maskType{1};                    // 0 none, 1 grille, 2 slot, 3 delta
    double pitch{0.30}, fill{0.85}, slotHeight{0.6}, slotGap{0.12}, damperWidth{0.05}, mix{1}, pitchPx{3};
    std::vector<double> dampers{1.0 / 3.0, 2.0 / 3.0};
    bool pixelLock{false};
    double convR[2]{0, 0}, convB[2]{0, 0}, radial{0};
    std::string screen{"p22"};          // colour screens take the SMPTE C primaries, D65, Rec.709 out
    double glassThickness{12}, glassN{1.52}, transmission{0.56}, albedo{0.5}, cell{1.0};
    double ambient{0.004}, reflection{0.6}, bezel[3]{0.035, 0.034, 0.033};
    bool interlace{false};
};
CrtParams crtPreset(const std::string& name);   // pvm-20, pvm-20-soft, trinitron-tv, slot-tv, vga-14, bw-tv, radar-p7, scope-p31

struct CrtHalo { int q{1}, R{0}; double cellMm{0}, direct{1}, kappa{0}, ringMm{0}; std::vector<float> weights; };
CrtHalo crtHaloKernel(const CrtParams& p, double mmPerPx, int maxCells);

struct CrtScreen { std::array<double, 9> matrix{}; bool mono{false}; std::array<std::array<double, 3>, 3> decay{}; };
CrtScreen crtScreen(const std::string& name);

enum CrtTap { TapRgb, TapEnc, TapSep, TapLum, TapI, TapQ };
struct CrtPlan {
    CrtParams p;
    int srcW{0}, srcH{0}, outW{0}, outH{0}, N{1}, lines{0};
    double fs{0}, faceW{0}, faceH{0}, rasterW{0}, rasterH{0}, lineMm{0}, distance{0}, viewW{0}, viewH{0};
    double lineCyc{0}, frameCyc{0}, field{0};
    std::vector<float> taps;                  // packTaps order: rgb, encodeC, lumaSep, lumaLp, demodI, demodQ (f32, as the passes read them)
    std::vector<double> taps64;               // the same before the f32 store (the plan link compares these)
    std::array<std::array<int, 2>, 6> tapInfo{};   // [offset, half]
    CrtScreen screen;
    std::array<std::array<double, 5>, 3> decay{};  // [w1, e1, e2, tau1, tau2]
    CrtHalo halo;
};
CrtPlan resolveCrt(const CrtParams& p, int srcW, int srcH, int outW, int outH);
// The parameter block of layout.mjs (122 values): float64 for the plan link, f32 as the passes read it.
std::vector<double> crtParamBlock(const CrtPlan& plan, int frame, double exposure = 1);
std::vector<float> crtPackParams(const CrtPlan& plan, int frame, double exposure = 1);
double crtMaskMean(const CrtParams& m);

// Tube geometry: face position and footprint half-extents of an output pixel; face alpha; the
// mask's per-colour transmission.
std::array<double, 4> crtFootprint(const CrtPlan& plan, int px, int py);
std::array<double, 3> crtMaskCover(const CrtPlan& plan, double sx, double sy, double hx, double hy, int px, int py);

struct CrtFrame { std::vector<float> sig, em, img; };
// One frame f of a source frame (R'G'B'A f32 rows, srcW x srcH); hist (outW x outH x 8) carries
// the persistence between frames and is updated in place.
CrtFrame crtFrame(const CrtPlan& plan, const std::vector<float>& src, std::vector<float>& hist, int f);
std::vector<float> crtSignal(const CrtPlan& plan, const std::vector<float>& src, int f);
std::vector<float> crtBeam(const CrtPlan& plan, const std::vector<float>& sig, std::vector<float>& hist, int field);
std::vector<float> crtGlass(const CrtPlan& plan, const std::vector<float>& em);
std::uint8_t crtEncode8(double v, double exposure = 1);

}  // namespace raw::style
