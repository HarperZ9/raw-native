#pragma once
// Private to the GI reference (src/renderer/gi_*.cpp): the bounce cache, the receiver rays of one
// frame, and the steps the driver in gi_cascades.cpp runs.
#include "raw/renderer/gi_cascades.hpp"
#include <cstdint>
#include <vector>
namespace raw::gi::detail {

// The bounce cache: a dense table over (cell, bucket); fixed-point frame sums, the blended E and
// the number of frames each entry has blended.
struct Cache {
    Grid grid;
    std::vector<std::uint32_t> sums;               // 4 a key: r, g, b, count
    std::vector<double> E;                         // 3 a key
    std::vector<std::uint32_t> seen;
};
Cache cacheOf(const Scene3& s, const Params& p);

// One receiver ray of a frame: its pixel, direction, hit distance (1e30 on escape), the first
// hit's cache key (kNone on escape) and albedo.
struct RayRec { std::uint32_t px{0}, py{0}; Vec3 dir; float t{1e30f}; std::uint32_t key{kNone}; Vec3 albedo; };

// The receivers' ray grid: one ray per stride x stride block, at the block's first pixel.
inline int raysAcross(int w, int stride){ return (w + stride - 1) / stride; }

// Frame f: trace every receiver ray and its first hit's sun and bounce rays, and add each first
// hit's estimate into the cache's sums. rays is resized to the ray grid.
void traceFrame(const Scene3& s, const GBuffer& g, const Params& p, int f, Cache& cache,
                std::vector<RayRec>& rays, int threads);
// Blend every entry's frame mean into E and clear the sums.
void resolveCache(Cache& cache);
// Split each receiver ray into the cascades (after this frame's resolve).
void splitRays(const GBuffer& g, const Params& p, const Cache& cache, const std::vector<RayRec>& rays,
               std::vector<Cascade>& cs, int threads);
// Merge the cascades from the coarsest down; then cascade 0's spherical harmonics and the gather.
void mergeCascades(std::vector<Cascade>& cs);
std::vector<double> shOf(const Cascade& c0);                     // 27 a slot: 9 coefficients x rgb
// 4 x 4 sub-directions a bin of an n x n octahedral grid, with solid-angle weights (1 / |p|^3 on
// the octahedron, normalised to 4 pi over the sphere). The GPU gets the same table.
struct SubDir { std::uint32_t bin; double x, y, z, w; };
std::vector<SubDir> subDirections(std::uint32_t n);
void gather(const GBuffer& g, const Cascade& c0, const std::vector<double>& sh, std::vector<double>& out, int threads);

inline std::uint32_t quantise(double v){ return v <= 0.0 ? 0u : std::uint32_t(v * kFixed + 0.5); }

}  // namespace raw::gi::detail
