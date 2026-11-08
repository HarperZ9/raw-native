#pragma once
// Loop writers (PROPOSALS P2; evidence/heldtime-loop-bounds.json): a palette GIF and an animated
// lossless WebP, both first party, plus the palette they share. Frames are RGB8 rows, top-down.
// Every writer takes a whole-number pixel scale (each pixel repeated k x k, never filtered).
// MP4 is written by scripts/loop_mp4.py through an external ffmpeg (ADR 0005: never linked).
//
// GIF: one global colour table, a NETSCAPE2.0 loop block, LZW per GIF89a. With delta on, a frame
// after the first carries only the rectangle that changed, with unchanged pixels transparent and
// disposal "leave in place", so held frames cost almost nothing.
// WebP: VP8L lossless frames (subtract-green transform, colour cache, Huffman codes; no LZ77) in a
// VP8X, ANIM and ANMF container; every frame is the full canvas, blending off.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::media {

using Rgb8 = std::array<std::uint8_t, 3>;
using Frame = std::vector<std::uint8_t>;   // w * h * 3

// Median cut over the colours of all frames (weighted by count), at most maxColours (<= 256).
std::vector<Rgb8> medianCut(const std::vector<Frame>& frames, int maxColours);
// Each pixel's nearest palette entry (squared RGB distance, lowest index on ties).
std::vector<std::uint8_t> mapNearest(const Frame& rgb, const std::vector<Rgb8>& palette);
Frame upscale(const Frame& rgb, int w, int h, int k, int channels = 3);

// Per-frame delays in units of 1/unitsPerSecond (GIF 100, WebP 1000), rounded so the sum over the
// loop is exact: delay_i = round((i + 1) u den / num) - round(i u den / num).
std::vector<int> frameDelays(int frames, std::int64_t fpsNum, std::int64_t fpsDen, int unitsPerSecond);

struct GifOptions { int w{0}, h{0}, scale{1}, loop{0}; bool delta{true}; std::vector<int> delaysCs; };
// indices: one w * h index image a frame (unscaled). Throws std::invalid_argument on bad input.
std::string encodeGif(const std::vector<Rgb8>& palette, const std::vector<std::vector<std::uint8_t>>& indices, const GifOptions& o);

struct GifImage {
    int w{0}, h{0}, loop{-1};
    std::vector<Rgb8> palette;
    std::vector<std::vector<std::uint8_t>> frames;   // composited canvases as palette indices
    std::vector<int> delaysCs;
};
// Decodes what encodeGif writes (global table, no interlace). False with err set otherwise.
bool decodeGif(const std::string& bytes, GifImage& out, std::string& err);

struct WebpOptions { int w{0}, h{0}, scale{1}, loop{0}; std::vector<int> delaysMs; int cacheBits{10}; };
std::string encodeWebp(const std::vector<Frame>& frames, const WebpOptions& o);
// One VP8L bitstream (no container) for a w x h RGB8 image; exposed for the tests.
std::string encodeVp8l(const Frame& rgb, int w, int h, int cacheBits);

bool writeFile(const std::string& path, const std::string& bytes);

}  // namespace raw::media
