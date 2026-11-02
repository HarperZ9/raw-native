#pragma once
// Spectral caustics through measured glass (effect C1, docs/hw-rt.md; bounds in
// evidence/hw-c1-caustic-bounds.json). One wavelength a path, drawn uniformly over 380 to 780 nm;
// glass refracts by its maker's Sellmeier formula at that wavelength, so white light splits into a
// spectrum. Two estimators add up to the image:
//   camera paths  next-event estimation at diffuse vertices, plus emitters seen with no diffuse
//                 vertex before them (directly or through glass);
//   light paths   from a lamp through at least one glass interaction to diffuse surfaces, each
//                 diffuse vertex joined to the pinhole camera (the caustics, sharp at low cost).
// Not covered: light that leaves a lamp through glass, scatters diffusely and reaches the camera
// through glass again (a caustic seen through the prism). That share is missing on both CPU and GPU.
// The BSDF-only estimator (CausticDesc::split false) counts every path and is the unbiased check.
//
// Output is CIE 1931 XYZ (2 degree observer) per pixel. Radiance is relative to the lamp: a lamp
// of power 1 has radiance S_A(lambda) / S_A(560 nm) inside its cone, S_A the CIE illuminant A.
#include "raw/math/vec.hpp"
#include "raw/rhi/rhi.hpp"
#include "raw/rhi/hw_rt.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rt {

// A glass by its maker's dispersion formula 2 of refractiveindex.info (Sellmeier, lambda in um):
// n^2 - 1 = sum B_i lambda^2 / (lambda^2 - C_i).
struct Glass { std::string id; double B[3], C[3]; double nd, vd; std::string provenance; };
double glassIndex(const Glass& g, double lambdaNm);
const Glass& glassNBK7();
const Glass& glassNSF11();

enum class CausticKind : std::uint32_t { Diffuse = 0, Glass = 1, Emitter = 2 };
struct CausticMaterial { CausticKind kind{CausticKind::Diffuse}; float albedo{0.8f}; std::uint32_t glass{0}, light{0}; };
// A one-sided rectangular lamp: emits from corner + s eu + t ev (s, t in [0, 1]) along +normal,
// inside a cone of half-angle coneRad about the normal. power scales its radiance.
struct CausticLight { Vec3 corner, eu, ev; float coneRad{0.1f}, power{1.0f}; };
struct CausticCamera { Vec3 eye{0, 1, 3}, target{0, 0, 0}, up{0, 1, 0}; float fovy{0.8f}; };

struct CausticScene {
    std::string name;
    std::vector<float> tris;           // 9 floats a triangle
    std::vector<float> normals;        // 9 floats a triangle: the shading normals at a, b, c
    std::vector<std::uint32_t> material;   // one a triangle
    std::vector<CausticMaterial> materials;
    std::vector<Glass> glasses;
    std::vector<CausticLight> lights;
    CausticCamera camera;
};
// The prism room: a 60 degree N-SF11 prism splitting a slit lamp into a spectrum on the floor, and a
// biconvex N-BK7 lens focusing a second lamp, with its chromatic fringe. view: 0 the showcase camera,
// 1 a camera that sees no glass (the estimator-agreement check needs one). lampScale multiplies both
// edges of each lamp and coneDeg replaces the lamps' 6 degree cones (K2 enlarges both).
CausticScene causticPrismRoom(int view = 0, float lampScale = 1.0f, float coneDeg = 6.0f);

struct CausticControls {
    bool noDispersion{false};   // D1: every wavelength takes the index at 587.56 nm (the d line)
    bool dropSplatCosine{false};   // D2: the light paths' join to the camera loses the surface cosine
    double indexOffset{0};      // added to every glass index (the reported resolving power)
};
struct CausticDesc {
    int width{640}, height{360};
    std::uint32_t spp{16};               // camera samples a pixel
    std::uint32_t photonsPerPixel{16};   // light paths per pixel (width * height * this in all)
    std::uint32_t maxBounces{12};
    std::uint64_t seed{1};
    bool split{true};                    // false: the BSDF-only estimator, no light paths
    CausticControls controls;
};
struct CausticImage {
    int width{0}, height{0};
    std::vector<double> xyz;             // 3 a pixel, camera and light estimators added
    std::vector<double> cameraXyz, lightXyz;
    std::string error;
    rhi::hw::PassTiming timing;          // hardware only
    std::vector<double> lightMs;         // hardware only
};
CausticImage causticCpu(const CausticScene& s, const CausticDesc& d, int threads = 0);
// Pixel centres of a w x h image whose first hit is glass (K2 needs a view with none).
int causticGlassPixels(const CausticScene& s, int w, int h);
CausticImage causticHw(rhi::Device& dev, const CausticScene& s, const CausticDesc& d, const float offset[3] = nullptr);

// Linear sRGB (D65 primaries) from XYZ with the lamp's white mapped to (1, 1, 1) by a diagonal
// scale in sRGB (the white balance of a camera set to tungsten).
std::vector<float> causticToRgb(const CausticImage& im, float exposure);
// XYZ of the illuminant A white of radiance 1 at 560 nm (the white the balance maps).
Vec3 causticLampWhite();

}  // namespace raw::rt
