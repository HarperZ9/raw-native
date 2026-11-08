// P2 loop writers: GIF round trip (B2, B4, K3), delays, palette, and the WebP container's shape.
// evidence/heldtime-loop-bounds.json. WebP pixel round trips are checked offline by
// scripts/loop_check.py with Pillow (B5), which is not a build dependency.
#include "check.hpp"
#include "raw/core/loop_media.hpp"
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>
using namespace raw::media;

namespace {
std::uint32_t lcg(std::uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
// Frames with held stretches (every pair of frames equal) and a moving block, over `colours` indices.
std::vector<std::vector<std::uint8_t>> makeIndices(int w, int h, int frames, int colours, std::uint32_t seed) {
    std::vector<std::vector<std::uint8_t>> out;
    for (int f = 0; f < frames; ++f) {
        std::vector<std::uint8_t> px(std::size_t(w) * h);
        std::uint32_t s = seed;
        const int step = f / 2;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const bool block = x >= step % w && x < step % w + 5 && y < 6;
            px[std::size_t(y) * w + x] = std::uint8_t(block ? (step % colours) : (lcg(s) % std::uint32_t(colours)));
        }
        out.push_back(px);
    }
    return out;
}
int offPalette(const GifImage& g, std::size_t paletteSize) {
    int n = 0;
    for (const auto& fr : g.frames) for (std::uint8_t v : fr) n += v >= paletteSize;
    return n;
}
void gifRoundTrip(int colours, int scale, bool delta) {
    const int w = 37, h = 23, frames = 9;
    std::vector<Rgb8> pal;
    for (int i = 0; i < colours; ++i) pal.push_back({std::uint8_t(i * 7), std::uint8_t(255 - i), std::uint8_t(i * 3 + 1)});
    const auto idx = makeIndices(w, h, frames, colours, 99u + std::uint32_t(colours));
    GifOptions o; o.w = w; o.h = h; o.scale = scale; o.delta = delta; o.delaysCs = frameDelays(frames, 24, 1, 100);
    GifImage g; std::string err;
    const bool ok = decodeGif(encodeGif(pal, idx, o), g, err);
    CHECK(ok);
    if (!ok) { std::printf("decode: %s\n", err.c_str()); return; }
    CHECK(g.w == w * scale && g.h == h * scale && int(g.frames.size()) == frames && g.loop == 0);
    bool same = true;
    for (int f = 0; f < frames; ++f) same = same && g.frames[std::size_t(f)] == upscale(idx[std::size_t(f)], w, h, scale, 1);
    CHECK(same);                                       // B4 at scale > 1, lossless LZW at every scale
    CHECK(offPalette(g, pal.size()) == 0);             // B2
    for (std::size_t i = 0; i < pal.size(); ++i) CHECK(g.palette[i] == pal[i]);
    CHECK(g.delaysCs == o.delaysCs);
}
void checkGif() {
    for (const int colours : {2, 5, 16, 200, 256}) for (const int scale : {1, 2, 3}) for (const bool delta : {false, true}) gifRoundTrip(colours, scale, delta);

    // LZW past 4096 codes: a large noisy frame forces code growth to 12 bits and clears.
    {
        const int w = 300, h = 200;
        std::vector<Rgb8> pal(256);
        for (int i = 0; i < 256; ++i) pal[std::size_t(i)] = {std::uint8_t(i), std::uint8_t(i), std::uint8_t(i)};
        const auto idx = makeIndices(w, h, 2, 256, 7u);
        GifOptions o; o.w = w; o.h = h;
        GifImage g; std::string err;
        CHECK(decodeGif(encodeGif(pal, idx, o), g, err) && g.frames.size() == 2 && g.frames[0] == idx[0] && g.frames[1] == idx[1]);
    }
}
void checkPaletteAndDelays() {
    // K3: one planted off-palette index is counted.
    {
        GifImage g; g.frames = {std::vector<std::uint8_t>(10, 1)};
        g.frames[0][4] = 9;
        std::printf("K3 planted off-palette pixels counted: %d (must be 1)\n", offPalette(g, 4));
        CHECK(offPalette(g, 4) == 1);
    }
    // Delays: rounding keeps the loop's total exact.
    const std::vector<int> d = frameDelays(144, 12, 1, 100), m = frameDelays(96, 24, 1, 1000);
    CHECK(std::accumulate(d.begin(), d.end(), 0) == 1200 && std::accumulate(m.begin(), m.end(), 0) == 4000);
    // Median cut: exact when there are fewer colours than slots, never more than asked.
    {
        Frame f;
        for (int i = 0; i < 40; ++i) f.insert(f.end(), {std::uint8_t(i * 6), std::uint8_t(i), std::uint8_t(250 - i)});
        CHECK(medianCut({f}, 64).size() == 40);
        CHECK(medianCut({f}, 8).size() == 8);
        const auto pal = medianCut({f}, 64);
        const auto idx = mapNearest(f, pal);
        bool exact = true;
        for (std::size_t i = 0; i < idx.size(); ++i) for (int k = 0; k < 3; ++k) exact = exact && pal[idx[i]][std::size_t(k)] == f[i * 3 + std::size_t(k)];
        CHECK(exact);
    }
}
void checkWebpShape() {
    // WebP container: RIFF size, VP8X animation flag, one ANMF a frame.
    {
        std::vector<Frame> frames(3, Frame(std::size_t(11) * 7 * 3, 90));
        frames[1][5] = 200;
        WebpOptions o; o.w = 11; o.h = 7; o.scale = 2; o.delaysMs = frameDelays(3, 12, 1, 1000);
        const std::string b = encodeWebp(frames, o);
        CHECK(b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0 && b.compare(12, 4, "VP8X") == 0);
        const std::uint32_t riff = std::uint32_t(std::uint8_t(b[4])) | std::uint32_t(std::uint8_t(b[5])) << 8 | std::uint32_t(std::uint8_t(b[6])) << 16 | std::uint32_t(std::uint8_t(b[7])) << 24;
        CHECK(riff + 8 == b.size());
        CHECK((std::uint8_t(b[20]) & 0x02) != 0);
        int anmf = 0;
        for (std::size_t p = b.find("ANMF"); p != std::string::npos; p = b.find("ANMF", p + 4)) ++anmf;
        CHECK(anmf == 3);
    }
}
}  // namespace

int main() {
    checkGif();
    checkPaletteAndDelays();
    checkWebpShape();
    return raw_test_summary();
}
