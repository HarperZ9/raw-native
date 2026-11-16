#pragma once
// Private to the ENB CRT and VHS port (raw/renderer/art_enbcrt.hpp): buffer reads, the hash
// streams, and the two pipelines, in float64.
#include "raw/renderer/art_enbcrt.hpp"
#include <array>
#include <cstdint>
namespace raw::art::enbcrt {

using C3 = std::array<double, 3>;
inline constexpr C3 kLum{0.2126, 0.7152, 0.0722};

// A display image read as the ENB host's samplers read it, clamped at the edges.
struct Image {
    const std::vector<float>* px{nullptr};
    int w{0}, h{0};
    C3 at(int x, int y) const;
    C3 linear(double u, double v) const;   // smpLinear: bilinear between texel centres
    C3 point(double u, double v) const;    // smpPoint: the texel the point falls in
};
struct Ctx {
    const EnbCrtParams& p;
    Image src, down;
    int frame{0}, x{0}, y{0};
    double time{0};
};

double unitHash(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t stream);
double smooth(double e0, double e1, double x);
C3 toYiq(const C3& c);
C3 fromYiq(const C3& c);
C3 sat(const C3& c);
double dot3(const C3& a, const C3& b);
int lineOf(int y, int h, int lines);   // floor(((y + 0.5) / h) * lines), exactly

C3 vhs(const Ctx& c, double& u, double& v);              // the VHS pipeline; moves (u, v)
C3 crt(const Ctx& c, double u, double v);                // the CRT pipeline at (u, v)

}  // namespace raw::art::enbcrt
