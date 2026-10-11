#pragma once
// raw-native's own triangle rasterizer (RT workstream stage R1, evidence/rt-r1-bounds.json).
// One specification, two forms: this deterministic CPU reference and the GPU compute
// rasterizer in src/renderer/gpu/shaders/swr.wgsl, which consumes the same setup records.
//
//   - 24.8 fixed-point vertices (8 sub-pixel bits); retro snapping to coarser grids.
//   - Sutherland-Hodgman clipping in clip space: near, far, and a guard band |x|,|y| <= G w.
//   - Integer edge functions, exact in 64 bits; the top-left fill rule; pixel-centre samples.
//   - Depth z/w interpolated in screen space, tested strictly less (ties keep the lower slot),
//     optionally quantized to D bits with a 4 x 4 ordered dither (retro).
//   - Perspective-correct or affine (retro) attributes; nearest, wrapping texels.
//
// Setup records are laid out exactly as the GPU reads them, so the same bytes can be fed to
// both rasterizers (check C4) or produced by each from the same vertices (check C5).
#include "raw/math/mat.hpp"
#include "raw/math/vec.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::swr {

inline constexpr int kSubpixelBits = 8;
inline constexpr int kSlotsPerTri = 7;          // a triangle clipped by six planes fans into at most 7
inline constexpr int kSetupInts = 12;           // x0 y0 x1 y1 x2 y2, source + 1 (0: empty), pixel bbox x0 y0 x1 y1, pad
inline constexpr int kSetupFloats = 16;         // z0..2, 1/w0..2, 1/area, (s, t) of each vertex in its source, pad
inline constexpr int kMaxSize = 2048;

enum class FillRule : std::uint32_t { TopLeft = 0, BottomRight = 1, Inclusive = 2, Exclusive = 3 };

struct Options {
    int snapShift{0};                // vertex grid 2^snapShift units of 1/256 px: 0 default, 8 whole pixels, 9 and 10 coarser
    bool affine{false};              // retro: screen-affine texture coordinates
    int depthBits{0};                // 0: float depth; 8..24: dithered integer depth of that many bits (retro)
    bool cullBack{false};            // drop triangles clockwise in NDC
    FillRule rule{FillRule::TopLeft};
    bool depthTest{true};            // false only for the C2 control (the last slot drawn wins)
    float guardBand{16.0f};          // clip |x|, |y| <= guardBand * w
};

// Procedural RGBA8 textures, every one a power of two on each side, packed one after another.
struct TextureSet {
    struct Entry { std::uint32_t offset, width, height; };
    std::vector<Entry> entries;
    std::vector<std::uint32_t> texels;          // RGBA8, r in the low byte
    std::uint32_t add(std::uint32_t w, std::uint32_t h, const std::vector<std::uint32_t>& px);
};

// Geometry as both rasterizers read it: one vertex array, one index array; per source
// triangle its texture. Attributes are per vertex.
struct Geometry {
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> uv;
    std::vector<std::uint32_t> idx;             // three a triangle
    std::vector<std::uint32_t> triTexture;      // one a triangle
    std::size_t triangles() const { return idx.size() / 3; }
};

struct Scene {
    std::string name;
    Geometry geo;
    TextureSet tex;
    Vec3 eye, target, up{0, 1, 0};
    float fovy{1.0f}, nearZ{0.1f}, farZ{200.0f};
    Vec3 light{0.4f, 0.8f, 0.45f};              // direction towards the light, normalised by the resolve
    Mat4 viewProj(int w, int h) const;
};

// Setup records: kSlotsPerTri slots a source triangle, kSetupInts and kSetupFloats each.
struct Setup {
    int width{0}, height{0};
    std::vector<std::int32_t> ints;
    std::vector<float> floats;
    std::vector<float> screen;                  // CPU only: unsnapped screen x, y of each slot's vertices (6 floats)
    std::size_t slots() const { return ints.size() / kSetupInts; }
};

// Transform, clip, snap and set up every triangle (the GPU's swr_setup does the same in float32).
Setup setup(const Geometry& g, const Mat4& viewProj, int w, int h, const Options& o);
// Setup of screen-space triangles given in pixels (z = 0.5, w = 1), for the fill-rule meshes.
Setup setupScreen(const std::vector<Vec2>& corners, int w, int h, const Options& o);

// The visibility buffer: per pixel the covering slot + 1 (0: none), its depth, and the
// integer depth used by the dithered mode.
struct Visibility {
    int width{0}, height{0};
    std::vector<std::uint32_t> slot;
    std::vector<float> depth;
    std::vector<std::uint32_t> depthQ;
    std::vector<std::uint32_t> count;           // coverage count (every covered sample, with or without the depth test)
};
Visibility rasterize(const Setup& s, const Options& o);

// Resolved attributes per pixel.
struct Resolved {
    std::vector<float> uv;                      // two a pixel
    std::vector<float> texelCoord;              // two a pixel: uv times the texture size, before the floor
    std::vector<std::uint32_t> texel;           // x | y << 12 | texture << 24, or 0xffffffff where nothing covers
    std::vector<std::uint32_t> colour;          // RGBA8
    std::vector<float> normal;                  // three a pixel, the unit interpolated normal (CPU resolve only)
};
Resolved resolve(const Setup& s, const Visibility& v, const Geometry& g, const TextureSet& t, Vec3 light, const Options& o);

// The 64-bit edge function and its exact float conversion, shared by the tests.
std::int64_t edge(std::int32_t xi, std::int32_t yi, std::int32_t xj, std::int32_t yj, std::int32_t px, std::int32_t py);
float edgeToFloat(std::int64_t e);              // e >= 0
bool topLeft(std::int32_t dx, std::int32_t dy);
bool covers(std::int64_t e, bool tl, FillRule r);
std::uint32_t bayer4(int x, int y);

// Uniform words as the GPU passes read them (the WGSL SwrParams struct, 36 words): sizes,
// options, the view-projection matrix (row-major) and the normalised light direction.
std::vector<std::uint32_t> params(const Options& o, int w, int h, std::uint32_t slots, int tile, const Mat4& viewProj,
                                  std::uint32_t triangles, Vec3 light);

}  // namespace raw::swr
