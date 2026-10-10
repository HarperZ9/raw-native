#pragma once
// Source-style baked lighting (ROADMAP M3, roadmap S10; Mitchell, McTaggart and Green,
// "Shading in Valve's Source Engine", SIGGRAPH 2006): radiosity normal maps with three
// tangent-space basis vectors, ambient cubes, and the CPU baker that fills them with a
// Lambertian path tracer. Bounds: evidence/m3-bake-bounds.json.
//
// Units follow raw/renderer/lighting.hpp: lights in candela, emission in nits. Every baked
// value is irradiance / pi (nits), so albedo times a value is the outgoing radiance.
#include "raw/renderer/bvh.hpp"
#include "raw/renderer/lighting.hpp"
#include <cstdint>
#include <vector>
namespace raw::bake {

using lighting::D3;
using lighting::Rgb;

// The three basis vectors in tangent space (z is the surface normal).
inline constexpr D3 kBasis[3] = {{-0.408248290463863, 0.707106781186548, 0.577350269189626},
                                 {-0.408248290463863, -0.707106781186548, 0.577350269189626},
                                 {0.816496580927726, 0.0, 0.577350269189626}};

// A one-sided rectangle: corner o, edges e1 and e2; its normal is normalize(e1 x e2).
struct Quad { D3 o, e1, e2; Rgb albedo, emission; };
struct Room {
    std::vector<Quad> quads;
    std::vector<lighting::Light> lights;            // point lights (candela)
};
Room testRoom();
// A closed cube of side 2 centred on the origin, every wall facing in with the same albedo and emission.
Room furnaceBox(double albedo, double emission);

// Path tracing in a room.
class Tracer {
public:
    explicit Tracer(const Room& room);
    // Irradiance / pi at p over the hemisphere above n, from `paths` cosine-weighted paths.
    // Without `withDirect` the point lights' direct light at p itself is left out (their
    // bounced light stays): Source shades important lights directly on models and keeps
    // only the rest in ambient cubes.
    Rgb irradiance(D3 p, D3 n, int paths, std::uint64_t seed, bool withDirect = true) const;
    // The point lights' direct irradiance / pi at p for normal n, with visibility.
    Rgb directLight(D3 p, D3 n) const { return direct(p, n, n); }
    // Irradiance / pi a surface facing b would receive at p, counting only light from the
    // hemisphere above the surface normal n and normalised by the part of the basis lobe
    // that lies above it (the radiosity-normal-map basis value; see bake.cpp).
    Rgb basisIrradiance(D3 p, D3 n, D3 b, int paths, std::uint64_t seed) const;
    const Room& room() const { return room_; }
    // The nearest surface along a ray: quad index or -1, and the distance.
    int trace(D3 o, D3 d, double& t) const;
private:
    struct Rng;
    Rgb incoming(D3 p, D3 dir, Rng& rng) const;
    Rgb direct(D3 p, D3 n, D3 b) const;            // point lights at p, weighted by max(0, l.b) over the hemisphere of n
    Room room_;
    Bvh bvh_;
    std::vector<int> quadOf_;                       // quad of each triangle
};

// One quad's directional lightmap: n1 x n2 texels, three basis values each.
struct Lightmap {
    int quad{0}, n1{0}, n2{0};
    std::vector<Rgb> basis[3];
    D3 texelCentre(const Room& room, int i, int j) const;
};
std::vector<Lightmap> bakeLightmaps(const Tracer& t, double texelsPerMetre, int paths, int threads);
// One texel: the three basis values, then scaled per channel so their mean equals the flat
// irradiance baked at the same texel. A flat normal then shades exactly whatever the light's
// direction, and a bumped normal keeps the basis values' ratios (an energy-preserving form
// of radiosity normal mapping, added 2026-10-10: the plain form's flat normal read 27% dark
// under a light along the normal).
void bakeTexel(const Tracer& t, Lightmap& lm, int i, int j, int paths, std::uint64_t seed);
// Shade a tangent-space normal from one texel: weights max(0, n.b_k)^2, normalised.
Rgb shadeRnm(const Lightmap& lm, int i, int j, D3 nTangent);
// Bilinear between texel centres (clamped at the chart's edges), at chart coordinates u, v in [0, 1].
Rgb shadeRnmAt(const Lightmap& lm, double u, double v, D3 nTangent);
// The quad's tangent frame: t along e1, then b = n x t.
void quadFrame(const Quad& q, D3& t, D3& b, D3& n);

// Six irradiances / pi for the axis normals, in order +x -x +y -y +z -z, without the point
// lights' direct light (shade it with Tracer::directLight or a dynamic light, as Source does).
struct AmbientCube { Rgb face[6]; };
AmbientCube bakeAmbientCube(const Tracer& t, D3 p, int paths, std::uint64_t seed);
Rgb shadeAmbientCube(const AmbientCube& c, D3 n);

}  // namespace raw::bake
