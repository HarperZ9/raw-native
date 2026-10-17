#pragma once
// The lens suite, the C++ core's float64 reference (ADR 0019) of web/shaders/lens (lens.mjs,
// plan.mjs, spectrum.mjs, coating.mjs, aperture.mjs, prescription.mjs). One HDR frame in, one out.
// Sources are the brightest tiles' over-threshold light; each kept tile gives up phi of that light
// (core pass), and the gather pass resamples the core per wavelength bin (lateral dispersion) and
// adds every source's ghosts (paraxial ray-transfer matrices of a three-element prescription,
// shaped by the iris), anamorphic streak, diffraction starburst (|FFT|^2 of the iris) and
// halation. Eight wavelength bins carry RGB weights from the CIE 1931 observer. The GPU passes
// are style_lens_core and style_lens_gather in style.wgsl.
#include <array>
#include <cstdint>
#include <vector>
namespace raw::style {

inline constexpr int kLensBins = 8;
struct LensIris { int blades{6}; double roundness{0.15}, rotation{0.3}; };
struct LensCoating { double n1{1.38}, quarterAt{550}, ns{1.52}; };
struct LensParams {
    double threshold{2.0}, refHeight{1080};
    int tile{16}, maxSources{24};
    double fNumber{2.8}, sensorHalfDiagMm{21.63}, ghostGain{25}, minGhostPx{2}, squeeze{1};
    double streakGain{0.05}, streakLenPx{140}, streakThickPx{0.9}, streakAngle{0};
    double starGain{0.03}, starCellPxPerStop{0.35}, haloGain{0.04}, haloRadiusPx{5}, caAmount{0.06};
    LensCoating coating;
    LensIris iris;
};
using LensBin3 = std::array<std::array<double, 3>, kLensBins>;
struct LensGhostTable { double f{0}, bfd{0}; std::vector<std::array<double, kLensBins>> m, rho; };
struct LensStar { int M{0}; std::vector<double> data; };   // M x M x 3, each channel sums to 1
struct LensPlan {
    LensParams p;                            // lengths scaled to the frame height
    int W{0}, H{0};
    double D{0}, cx{0}, cy{0}, tanHalf{0}, Rbar{0}, starCell{0};
    std::vector<std::array<std::array<double, 4>, kLensBins>> ghosts;   // per pair and bin: m, radius px, rotation, 1 / area
    std::array<double, kLensBins> R0{}, caScale{};
    std::array<std::array<double, 2>, kLensBins> halo{};               // gain, radius px
    std::array<double, 3> phi{};
    LensBin3 binRgb{};
    LensStar star;
};
struct LensSource { double x{0}, y{0}; std::array<double, 3> E{}; double theta{0}; std::array<double, kLensBins> ghost{}, streak{}; };
struct LensRawSource { double x{0}, y{0}; std::array<double, 3> E{}; };

LensBin3 lensBinRgb();
LensGhostTable lensGhostTable();
double lensFilmReflectance(double nm, double theta, const LensCoating& c);
double lensShapeArea(const LensIris& iris);
LensStar lensStarburst(const LensIris& iris);
LensPlan resolveLens(const LensParams& p, int W, int H);
// frame: RGBA float64 rows. Returns the kept sources and the keep mask (one byte a tile).
std::vector<LensRawSource> extractLensSources(const LensPlan& plan, const std::vector<double>& frame, std::vector<std::uint8_t>& keep);
std::vector<LensSource> prepareLensSources(const LensPlan& plan, const std::vector<LensRawSource>& list);
// keep may be empty (list mode: nothing is removed). Returns RGBA float64, alpha 1.
std::vector<double> runLens(const LensPlan& plan, const std::vector<double>& frame, const std::vector<LensSource>& srcs, const std::vector<std::uint8_t>& keep);

}  // namespace raw::style
