#pragma once
// Colour: tone mappers and display output transforms, the CPU reference for the
// WGSL in web/colour/colour.wgsl (roadmap M1, exit criterion 2).
//
// Input is scene-linear light with Rec.709 primaries and a D65 white, the
// engine's working space. A pipeline is a tone mapper followed by an output
// encoding, named "<tone>/<output>", for example "aces2-sdr/srgb".
//
//   tone:   clip, pbr-neutral, agx, aces2-sdr (100 nits), aces2-hdr1000 (1,000 nits)
//   output: srgb, display-p3, rec2020 (BT.1886, pure 2.4), rec2100-pq (ST 2084)
//
// The bounds against OpenColorIO 2.6.0, and the grid they are measured on, are
// in evidence/m1-colour-bounds.json. The ACES 2.0 output transform is a port of
// OpenColorIO's implementation (BSD-3-Clause, see src/renderer/aces2.hpp).
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace raw::colour {

using RGB = std::array<float, 3>;
using Mat3 = std::array<double, 9>;          // row-major

struct Primaries { double r[2], g[2], b[2], w[2]; };
extern const Primaries kRec709, kP3D65, kRec2020, kAP0, kAP1;

Mat3 rgbToXyz(const Primaries& p);                       // white maps to Y = 1
Mat3 inverse(const Mat3& m);
Mat3 multiply(const Mat3& a, const Mat3& b);
// src RGB to dst RGB; Bradford adaptation when the white points differ.
Mat3 conversion(const Primaries& src, const Primaries& dst, bool bradford = true);
RGB apply(const Mat3& m, const RGB& v);

// Transfer functions on display-relative values.
double srgbEncode(double linear);            // IEC 61966-2-1, input clamped at 0
double srgbDecode(double encoded);
double bt1886Encode(double linear);          // pure 2.4 power, input clamped at 0
double bt1886Decode(double encoded);
double pqEncode(double linear100);           // SMPTE ST 2084; 1.0 = 100 nits
double pqDecode(double encoded);             // returns 1.0 = 100 nits

// Tone mappers on scene-linear Rec.709, returning display-linear Rec.709.
RGB pbrNeutral(const RGB& c);                // Khronos PBR Neutral
RGB agx(const RGB& c);                       // AgX, Filament and three.js form

enum class Tone { Clip, PbrNeutral, AgX, Aces2 };
enum class Output { Srgb, DisplayP3, Rec2020, Rec2100Pq };

struct Pipeline {
    Tone tone = Tone::Clip;
    Output output = Output::Srgb;
    float peakNits = 100.0f;                 // ACES 2.0 only
    std::string name;
};
// Parses "<tone>/<output>"; false for an unknown name or an unsupported pairing.
bool parsePipeline(std::string_view name, Pipeline& out);
std::vector<std::string> pipelineNames();

namespace aces2 { struct Transform; }

// A pipeline ready to run. Building an ACES 2.0 pipeline computes its hue tables
// once; apply() is then a pure function of the input.
class Transform {
public:
    explicit Transform(const Pipeline& p);
    ~Transform();
    RGB apply(const RGB& sceneLinear709) const;   // encoded display values in [0,1]
    const Pipeline& pipeline() const { return p_; }
    // The ACES 2.0 parameters and tables, as JSON, for the GPU (null otherwise).
    std::string aces2Json() const;
private:
    Pipeline p_;
    Mat3 outMatrix_{};                        // display-linear Rec.709 (or limiting) to output
    std::unique_ptr<aces2::Transform> aces_;
};

// The committed input grid of evidence/m1-colour-bounds.json, in its order:
// the 33^3 cube (red fastest), the grey ramp, then the saturation ramps.
std::vector<RGB> grid();

}
