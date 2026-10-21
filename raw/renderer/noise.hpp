#pragma once
// The engine's one noise module (ROADMAP P8; evidence/p8-noise-dither-bounds.json). Float32 with
// + - * / floor and sqrt only, so a WGSL pass can mirror it line for line. Every function is a
// pure function of its position and seed.
//
//   white         uniform [0, 1) from an integer lattice point
//   value         lattice values, quintic fade, [0, 1)
//   perlin        gradient noise (quintic fade; 8 gradients in 2D, 12 in 3D), about [-1, 1]
//   simplex       simplex noise from the skew and kernel definitions, about [-1, 1]
//   worley        cellular: distance to the nearest (F1) and second nearest (F2) feature point
//   fbm, ridged, billow, hybrid   octave sums of a base noise (Musgrave's forms for ridged and hybrid)
//   warp          domain warp: the position displaced by two fBm fields
//   blue          the 64 x 64 void-and-cluster mask, rank to [0, 1), tiled
#include <cstdint>
namespace raw::noise {

std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed);   // lowbias32 chain
float white(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed);

float value2(float x, float y, std::uint32_t seed);
float value3(float x, float y, float z, std::uint32_t seed);
float perlin2(float x, float y, std::uint32_t seed);
float perlin3(float x, float y, float z, std::uint32_t seed);
float simplex2(float x, float y, std::uint32_t seed);
float simplex3(float x, float y, float z, std::uint32_t seed);
struct Cell { float f1, f2; };
Cell worley2(float x, float y, std::uint32_t seed);
Cell worley3(float x, float y, float z, std::uint32_t seed);

// The base noise an octave sum calls: perlin, simplex or value (value mapped to [-1, 1]).
enum class Base : std::uint8_t { Perlin, Simplex, Value };
float base3(Base b, float x, float y, float z, std::uint32_t seed);
struct Octaves { int count{5}; float lacunarity{2.0f}, gain{0.5f}; };
float fbm(Base b, float x, float y, float z, std::uint32_t seed, Octaves o = {});      // normalised by the amplitude sum, about [-1, 1]
float ridged(Base b, float x, float y, float z, std::uint32_t seed, Octaves o = {});   // [0, 1]
float billow(Base b, float x, float y, float z, std::uint32_t seed, Octaves o = {});   // about [-1, 1]
float hybrid(Base b, float x, float y, float z, std::uint32_t seed, Octaves o = {}, float offset = 0.7f);   // >= 0, unnormalised
struct Warped { float x, y, z; };
Warped warp(float x, float y, float z, std::uint32_t seed, float strength, Octaves o = {});

float blue(std::int32_t x, std::int32_t y);   // the mask's rank at (x mod 64, y mod 64), (rank + 0.5) / 4096

}  // namespace raw::noise
