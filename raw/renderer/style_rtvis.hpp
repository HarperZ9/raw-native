#pragma once
// One-ray area-light visibility through the dither threshold, the C++ core's reference (ADR 0019,
// rule 6: CPU code with no shader of its own) of web/shaders/ditherlight/rtvis.mjs and the
// analytic Penumbra Yard of web/shaders/fixtures/penumbra.mjs. The light sample of each pixel is
// point r of a (0, 6, 3)-net in base 2 chosen by the pixel's Bayer rank: (r / 64, phi(r), psi(r)),
// phi the radical inverse of r's six bits and psi the Pascal matrix mod 2 on those digits (Faure),
// so over any 8 x 8 tile the (threshold, sample) pairs fill every elementary box once.
#include <array>
#include <string>
#include <vector>
namespace raw::style {

int rtvisBitrev6(int r);
int rtvisPascal6(int r);
int rtvisBayer8Rank(int x, int y);
std::array<double, 2> rtvisConcentric(double u, double v);   // Shirley and Chiu's square-to-disc map
using RV3 = std::array<double, 3>;
RV3 rtvisLightPoint(const RV3& C, double R, const RV3& P, double u, double v);
struct YardLight { RV3 pos{0.3, 3, -0.6}; double radius{0.6}; };
struct YardBlocker { bool box{true}; RV3 lo{}, hi{}, c{}; double r{0}; };
YardBlocker yardBlocker(const std::string& name);              // wall-x, wall-z, sphere
bool yardOccluded(const YardBlocker& b, const RV3& P, const RV3& Q);
// The smallest distance of any of yardOccluded's decisions from flipping (a tie when tiny).
double yardOcclusionMargin(const YardBlocker& b, const RV3& P, const RV3& Q);
// The light points an estimator samples for pixel (x, y) at frame f.
std::vector<RV3> rtvisSamples(const std::string& method, const YardLight& L, const RV3& P, int x, int y, int f);
RV3 yardPoint(int x, int y);                                   // pixel (x, y) of the 256 x 256 yard
double yardTone(const RV3& P, const YardLight& L, double V);
// Visibility of the light from P by an estimator (net1, rank1, white1, ref64) for pixel (x, y), frame f.
double rtvisVisibility(const std::string& method, const YardLight& L, const RV3& P, int x, int y, int f, const YardBlocker& b);

}  // namespace raw::style
