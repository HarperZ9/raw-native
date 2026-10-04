#pragma once
#include "raw/math/vec.hpp"
#include "raw/core/image.hpp"
#include "raw/core/arena.hpp"
#include <cstdint>
namespace raw {
struct GBuffer {
    int w{0}, h{0};
    Buffer<float> depth;
    Buffer<Vec3> normal, position, albedo;
    Buffer<uint8_t> mask;
    // Screen-space velocity per pixel (extra channel): current minus previous
    // clip-space UV, in [0,1] UV units. Zero for a static camera. See raw/motion.hpp.
    Buffer<Vec2> motion;
    GBuffer() = default;
    explicit GBuffer(Arena* a)
        : depth(a), normal(a), position(a), albedo(a), mask(a), motion(a) {}
    void resize(int W,int H){ w=W;h=H;
        depth.resize(W,H); normal.resize(W,H); position.resize(W,H);
        albedo.resize(W,H); mask.resize(W,H); motion.resize(W,H); }
};
}
