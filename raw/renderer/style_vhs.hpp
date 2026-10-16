#pragma once
// VHS in the signal domain, the C++ core's reference (ADR 0019) of web/shaders/vhs/vhs.mjs, whose
// header carries the model: a line of R'G'B' sampled at 4 fsc with the transport's time-base
// displacement (pass 1), luma through the FM channel and chroma through colour-under with noise,
// delay and phase wobble (pass 2, tape), dropouts filled from the line above and Y'IQ back to
// R'G'B' (pass 3, playback), and a display resample to RGBA8. The JavaScript stores each pass's
// output as f32 and computes in float64 between; so does this. The GPU passes are
// style_vhs_sample, style_vhs_tape, style_vhs_play and style_vhs_show in style.wgsl.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct VhsParams {
    double lumaMHz{3}, chromaMHz{0.5}, lumaNoise{0.012}, chromaNoise{0.01}, jitter{0.25}, peaking{0.35}, dropouts{0.002}, chromaDelayNs{150}, phaseNoiseDeg{1.5};
    int generations{1};
    double fsc{315.0 / 88.0}, pixelClockMHz{13.5};
    int headSwitchLines{6};
    double headSkew{6}, tracking{0};
    int seed{1};
};
VhsParams vhsPreset(const std::string& name);   // sp-fresh, lp-worn, ep-rental, thriller
struct VhsPlan {
    VhsParams p;
    double fs{0};
    int N{1}, lines{0}, srcW{0}, srcH{0}, g{1};
    std::vector<double> luma, peak, chroma;   // FIR taps, each 2 half + 1 long
    double lumaNoise{0}, chromaNoise{0}, delay{0}, phaseNoise{0};
    double chromaKnot{24};   // samples between chroma-noise knots; a source-edit control changes it in the code
};
VhsPlan resolveVhs(const VhsParams& p, int srcW, int srcH);
extern const std::array<double, 9> kYiq;
std::array<double, 9> vhsYiqInverse();   // inv3 of kYiq (web/shaders/spectral.mjs)
// A Box-Muller normal from three integer hashes (gauss of vhs.mjs).
double vhsGauss(std::uint32_t a, std::uint32_t b, std::uint32_t c);
double vhsLineShift(const VhsPlan& plan, int y, int f);
double vhsTrackingBand(const VhsPlan& plan, int y, int f);
// The three passes of one frame: src is R'G'B'A f32 rows (srcW x srcH); returns the signal,
// lines x N x 4 (R'G'B', dropout flag).
std::vector<float> vhsFrame(const VhsPlan& plan, const std::vector<float>& src, int f);
// toDisplay: the signal resampled to w x h RGBA8.
std::vector<std::uint8_t> vhsDisplay(const VhsPlan& plan, const std::vector<float>& sig, int w, int h);

}  // namespace raw::style
