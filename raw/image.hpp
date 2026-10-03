#pragma once
#include "raw/vec.hpp"
#include "raw/arena_allocator.hpp"
#include <vector>
#include <string>
#include <cstdint>
namespace raw {
template<class T> struct Buffer {
    int w{0}, h{0};
    std::vector<T, ArenaAllocator<T>> px;
    Buffer() = default;                                  // heap (default allocator)
    explicit Buffer(Arena* a) : px(ArenaAllocator<T>(a)) {}  // arena-backed
    void resize(int W, int H){ w=W; h=H; px.assign((size_t)W*H, T{}); }
    T& at(int x,int y){ return px[(size_t)y*w + x]; }
    const T& at(int x,int y) const { return px[(size_t)y*w + x]; }
};
void writePPM(const Buffer<Vec3>& img, const std::string& path);
void writePGM(const Buffer<float>& img, const std::string& path);
// Portable Float Map (PF, little-endian): the HDR channel written verbatim as
// 32-bit floats, no clamp and no gamma, so the model's exact linear radiance
// survives to disk. Standard format, no third-party dependency.
void writePFM(const Buffer<Vec3>& img, const std::string& path);
// Single-channel PFM ("Pf"), little-endian, rows bottom-to-top: a float buffer
// written at full 32-bit precision. Used for the two AO buffers so a checker
// can recompute the reconcile exactly from the files.
void writePFM1(const Buffer<float>& img, const std::string& path);
// Coverage mask as 8-bit PGM: 255 where a surface covers the pixel, 0 elsewhere.
void writeMaskPGM(const Buffer<uint8_t>& mask, const std::string& path);
// Readers for the two formats above. They accept only what the writers emit
// and return false on any other header, size or truncation.
bool readPFM1(const std::string& path, Buffer<float>& out);
bool readMaskPGM(const std::string& path, Buffer<uint8_t>& out);
}
