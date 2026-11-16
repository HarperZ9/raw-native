#pragma once
// The ENB CRT and VHS fast tier (evidence/art-enb-crt-bounds.json): a port of the author's ENB addon
// "CRT / VHS Display Simulation Addon v3.0" (ENB of the Elders, Zain Dana Harper;
// shader-handoff/elder-enb/installed-enbseries/Addons/Effect_CRTShader.fxh, sha256 93d82676...).
// One pass: the VHS pipeline (tracking, jitter, YIQ chroma blur, chroma smear, head switching,
// dropouts and snow, generation loss) then the CRT pipeline (curvature, overscan, convergence,
// colour profile, brightness, contrast, saturation, scanline beam, phosphor mask, bloom, halation,
// signal noise, corner shadow), on display-encoded RGB in [0, 1]. This is the float64 reference;
// the GPU passes are art_enbcrt_down and art_enbcrt in src/renderer/gpu/shaders/art_enbcrt.wgsl and
// art_enbcrt_crt.wgsl. Faithful to the original, with both pipelines on, the CRT resamples the input
// and keeps only the VHS displacement; chaining a VHS pass into a CRT pass passes the tape image on.
// Deviations from the original (the bounds file lists them): integer hashes in place of sin hashes,
// explicit bilinear and nearest buffer reads, Rec. 709 luma for the host's K_LUM, time as frame / 30.
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::art {

struct EnbCrtParams {
    bool crt{false};
    double curvature{0.05}, overscan{0.01}, brightness{1.1}, contrast{1.05}, saturation{1.15};
    double scanIntensity{0.30}, scanWidth{2.0}, beamWidth{0.50};
    int maskType{0};   // 0 aperture grille, 1 slot, 2 shadow
    double maskIntensity{0.40}, maskScale{1.0}, bloom{0.15}, convergence{0.0}, halation{0.10}, cornerRadius{0.03}, signalNoise{0.0};
    int colourProfile{0};   // 0 sRGB, 1 P22, 2 Trinitron, 3 cool white
    bool vhs{false};
    double chromaBlur{0.60}, chromaSmear{0.30}, tracking{0.10}, headSwitch{0.30}, tapeNoise{0.05}, snow{0.03}, jitter{0.15};
    int quality{0}, generation{0};   // 0 SP, 1 LP, 2 EP; copy generation 0..5
};
// crt, crt-slot, crt-shadow, vhs, vhs-crt, vhs-ep, off, crt-zero (every CRT term at zero).
EnbCrtParams enbCrtPreset(const std::string& name);

// The 128 x 128 average the ENB host supplies as RenderTarget128: each texel is the mean of the
// source pixels whose centres fall in it. RGBA f32.
std::vector<float> enbCrtDown(const std::vector<float>& img, int w, int h);
// One frame: img is display RGBA f32 rows; frame sets the time (frame / 30 s). Returns display RGBA
// in [0, 1] (alpha 1).
std::vector<float> runEnbCrt(const EnbCrtParams& p, const std::vector<float>& img, int w, int h, int frame);

}  // namespace raw::art

namespace raw::gpu_check {
struct EnbCrtGpu { std::string error; std::vector<std::uint32_t> packed; double ms{0}; };
// The two passes on `dev` (RGBA8 codes a pixel); with repeats > 0 also their warm time, batched
// 10 a submission.
bool enbCrtOnGpu(rhi::Device& dev, const art::EnbCrtParams& p, const std::vector<float>& img, int w, int h, int frame, EnbCrtGpu& out, int repeats);
}  // namespace raw::gpu_check
