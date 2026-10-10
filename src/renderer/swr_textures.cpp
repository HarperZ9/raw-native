// Procedural textures for the rasterizer's owned scenes: see raw/renderer/swr_scenes.hpp.
// Integer hashing only, so every build produces the same texels.
#include "raw/renderer/swr_scenes.hpp"
#include <algorithm>
#include <cmath>
namespace raw::swr {
namespace {
constexpr std::uint32_t kN = 64;
std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
std::uint32_t h2(std::uint32_t x, std::uint32_t y, std::uint32_t s) { return hash(x * 0x9e3779b1u ^ hash(y + s * 0x85ebca6bu)); }
// Value noise on a lattice of `cell` texels, wrapping at kN, in 0..255.
int noise(std::uint32_t x, std::uint32_t y, std::uint32_t cell, std::uint32_t seed) {
    const std::uint32_t n = kN / cell, cx = x / cell, cy = y / cell, fx = x % cell, fy = y % cell;
    const auto v = [&](std::uint32_t i, std::uint32_t j) { return int(h2(i % n, j % n, seed) & 255u); };
    const int a = v(cx, cy), b = v(cx + 1, cy), c = v(cx, cy + 1), d = v(cx + 1, cy + 1);
    const int top = a + (b - a) * int(fx) / int(cell), bot = c + (d - c) * int(fx) / int(cell);
    return top + (bot - top) * int(fy) / int(cell);
}
std::uint32_t rgb(int r, int g, int b) {
    const auto c = [](int v) { return std::uint32_t(std::clamp(v, 0, 255)); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | 0xff000000u;
}
using Fn = std::uint32_t (*)(std::uint32_t, std::uint32_t);
std::vector<std::uint32_t> make(Fn f) {
    std::vector<std::uint32_t> px(kN * kN);
    for (std::uint32_t y = 0; y < kN; ++y) for (std::uint32_t x = 0; x < kN; ++x) px[y * kN + x] = f(x, y);
    return px;
}
std::uint32_t brick(std::uint32_t x, std::uint32_t y) {
    const std::uint32_t row = y / 8, off = (row & 1) * 8, bx = ((x + off) % kN) / 16;
    const bool mortar = (y % 8) == 7 || ((x + off) % 16) == 15;
    const int n = noise(x, y, 4, 11) / 8;
    if (mortar) return rgb(150 + n, 140 + n, 125 + n);
    const int t = int(h2(bx, row, 3) % 40);
    return rgb(140 + t + n, 58 + t / 2 + n / 2, 42 + n / 2);
}
std::uint32_t tiles(std::uint32_t x, std::uint32_t y) {
    if (x % 32 == 0 || y % 32 == 0) return rgb(60, 52, 46);
    const bool dark = ((x / 32) + (y / 32)) & 1;
    const int n = noise(x, y, 8, 5) / 10;
    return dark ? rgb(150 + n, 82 + n, 56 + n) : rgb(214 + n, 196 + n, 160 + n);
}
std::uint32_t planks(std::uint32_t x, std::uint32_t y) {
    if (x % 16 == 0) return rgb(48, 30, 20);
    const int grain = noise(x * 4 % kN, y, 16, 7 + x / 16) / 4 + int((y * 3 + x * 7) % 9);
    const int tone = int(h2(x / 16, 0, 9) % 30);
    return rgb(120 + tone + grain, 80 + tone / 2 + grain / 2, 48 + grain / 3);
}
std::uint32_t checker(std::uint32_t x, std::uint32_t y) { return ((x / 8 + y / 8) & 1) ? rgb(230, 230, 220) : rgb(40, 44, 60); }
std::uint32_t stone(std::uint32_t x, std::uint32_t y) {
    const int n = (noise(x, y, 16, 21) + noise(x, y, 4, 22) / 2) / 2;
    const bool seam = x % 32 == 0 || y % 32 == 0;
    return seam ? rgb(50, 54, 58) : rgb(88 + n / 3, 96 + n / 3, 104 + n / 3);
}
std::uint32_t plaster(std::uint32_t x, std::uint32_t y) {
    const int n = noise(x, y, 16, 31) / 5, stain = std::max(0, noise(x, y, 32, 33) - 170) / 2;
    return rgb(196 + n - stain, 172 + n - stain, 128 + n / 2 - stain);
}
std::uint32_t cloth(std::uint32_t x, std::uint32_t y) {
    const int weave = ((x + y) & 1) ? 10 : -10;
    return rgb(52 + weave, 70 + weave, 96 + noise(x, y, 8, 41) / 8 + weave);
}
std::uint32_t skin(std::uint32_t x, std::uint32_t y) { const int n = noise(x, y, 8, 51) / 12; return rgb(200 + n, 150 + n, 120 + n); }
std::uint32_t brass(std::uint32_t x, std::uint32_t y) { const int n = noise(x, y, 4, 61) / 6; return rgb(190 + n, 150 + n, 70 + n / 2); }
}  // namespace

TextureSet proceduralTextures() {
    TextureSet t;
    for (Fn f : {brick, tiles, planks, checker, stone, plaster, cloth, skin, brass}) t.add(kN, kN, make(f));
    return t;
}

}  // namespace raw::swr
