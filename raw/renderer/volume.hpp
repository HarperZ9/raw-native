#pragma once
// Volumetric clouds and smoke (ROADMAP P14; evidence/p14-clouds-bounds.json), float64 on the CPU.
//   Density: a soft union of sphere lobes under a height gradient (flat base), eroded by the noise
//   module's Worley F1 and billow noise (raw/renderer/noise.hpp).
//   march: the engine's look. Beer transmittance to the sun by a secondary march; a dual-lobe
//   Henyey-Greenstein phase; an optional Beer-powder term (artistic, not energy-conserving); sky
//   ambient; an optional multiple-scattering approximation by octaves (after Wrenninge et al.).
//   Each step integrates in-scattering over its length exactly for a constant density (Hillaire 2015).
//   pathTrace: the reference. Delta tracking against the density's majorant, next-event estimation
//   to the sun with ratio tracking, phase sampling, single- or multiple-scattering modes.
// World space, +Y up, metres; radiance in the sky model's nits.
#include "raw/renderer/lighting.hpp"
#include "raw/renderer/sky.hpp"
#include <cstdint>
#include <vector>
namespace raw::volume {

using lighting::D3;
using lighting::Rgb;

struct Lobe { D3 c; double r{1}, w{1}; };
struct Cloud {
    std::vector<Lobe> lobes;
    double base{0}, top{1};                        // the height gradient's span (flat base, faded top)
    double sigma{8};                               // extinction per metre at density 1 (the majorant)
    double albedo{0.98};                           // single-scattering albedo
    double worleyScale{1.6}, worleyAmount{0.35};   // erosion: cauliflower edges
    double billowScale{3.0}, billowAmount{0.12};   // erosion: wisps
    std::uint32_t seed{7};
    bool noiseBlob{false};                         // the review's control: fBm coverage, no lobes
    D3 lo{-1, -1, -1}, hi{1, 1, 1};                // bounds every lobe lies in
};
double density(const Cloud& c, D3 p);              // in [0, 1]

struct Lighting {
    sky::Clear sky;                                // the sky (and its sun) behind and around
    bool skyOn{true};
    double uniformEnv{-1};                         // >= 0: a uniform environment of that radiance instead (furnace)
    bool sunOn{true};
    double ambientScale{1};                        // march only: the sky's ambient in-scattering
};
Rgb environment(const Lighting& l, D3 dir);
Rgb sunIlluminance(const Lighting& l);
double phase(double cosTheta, double g1, double g2, double mix);   // dual-lobe Henyey-Greenstein, 1/sr

struct Camera { D3 eye{0, 1, 6}, target{0, 1, 0}; double fovy{0.8}; };
struct MarchDesc {
    double step{0.05};                             // metres along the view ray
    int sunSteps{24};
    double g1{0.6}, g2{-0.25}, mix{0.75};
    bool powder{false}, ambient{false};
    int octaves{1};                                // 1: single scattering only
};
// The camera ray through the centre of pixel (x, y).
D3 cameraRay(const Camera& cam, int w, int h, int x, int y);
// Radiance per pixel (pixel centres), and optionally the cloud's alpha (1 - transmittance).
std::vector<Rgb> march(const Cloud& c, const Lighting& l, const Camera& cam, int w, int h, const MarchDesc& d, std::vector<double>* alpha = nullptr, int threads = 0);
// The reference, at pixel centres: spp paths a pixel; var receives the luminance variance of the mean.
std::vector<Rgb> pathTrace(const Cloud& c, const Lighting& l, const Camera& cam, int w, int h, int spp, bool singleScattering,
                           const MarchDesc& phaseOf, std::vector<double>* var = nullptr, std::uint64_t seed = 1, int threads = 0);

// The goldens' scenes.
enum class Scene : std::uint8_t { Cumulus, Towering, Deck, SmokeSunset };
const char* name(Scene s);
void scene(Scene s, Cloud& c, Lighting& l, Camera& cam);

}  // namespace raw::volume
