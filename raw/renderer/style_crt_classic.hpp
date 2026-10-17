#pragma once
// crt-classic, the C++ core's reference (ADR 0019): the Studio's tube stage, the author's
// retro-crt.js (vendored as web/shaders/reference/retro-crt.mjs). Beam scanlines and a phosphor
// mask in 8.8 fixed point, bloom and halation blurred in linear light at a quarter (or half)
// resolution, then one tube pass: barrel warp with bilinear 8.8 taps, a feathered rounded bezel,
// radial colour separation and the vignette. RGBA8 in and out. The JavaScript is integer and
// Float32Array arithmetic; every f32 store and integer truncation it makes is made here too. The
// GPU passes are style_crt_phosphor, style_crt_down, style_crt_box, style_crt_add and
// style_crt_tube in src/renderer/gpu/shaders/style.wgsl.
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct ClassicOptions {
    int cell{1};
    bool scanlines{false};
    double scanStrength{0}, beam{0.5};
    std::string mask{"none"};   // none, grille, slot or dot
    double maskStrength{0}, bloom{0}, halation{0}, curvature{0}, aberration{0}, vignette{0};
};
// The sRGB transfer tables: 256 codes to linear (f32), 4097 linear steps to codes.
struct ClassicTables { std::vector<float> toLin; std::vector<std::uint8_t> toSrgb; };
const ClassicTables& classicTables();
std::vector<float> classicBeamTable(int cell, double strength, double beam);   // 16 x cell row factors
std::vector<float> classicMaskTable(const std::string& mode, double strength);  // 6 x 3 x 3 channel weights
// The 8.8 fixed tables the phosphor pass reads for these options (beamTable or ones, maskTable or ones).
std::vector<std::int32_t> classicBeamFixed(const ClassicOptions& o);
std::vector<std::int32_t> classicMaskFixed(const ClassicOptions& o);
// The box radius blur3 uses for a sigma (in low-resolution pixels).
int classicBoxRadius(double sigma);
// The tube stage in place on RGBA8 rows (crtStage).
void crtClassic(std::vector<std::uint8_t>& rgba, int w, int h, const ClassicOptions& o);

}  // namespace raw::style
