#pragma once
// raw_native_cli held-time: the P1 and P2 demonstration and its loop measurements
// (evidence/heldtime-loop-bounds.json B1 to B4). Renders the held-time hatch scene on ones, twos
// and threes as closed loops, writes stills (PNG), palette GIFs, lossless WebPs and raw RGB for
// scripts/loop_finish.py (MP4 and the Pillow checks), and a run JSON.
//   raw_native_cli held-time [--out DIR] [--raw DIR] [--w 960] [--h 540] [--frames 96] [--fps 24]
//                            [--threads 3] [--palette 64] [--quick]
// --quick renders at 160 x 90 and skips the size loop (a smoke run). A loop whose terms cannot
// close (raw/scene/loop.hpp) is refused before any rendering.
#include "raw/core/loop_media.hpp"
#include "raw/renderer/heldtime_scene.hpp"
#include <string>
#include <vector>
namespace raw {

struct LoopRender {
    std::string name;
    int w{0}, h{0};
    std::vector<media::Frame> frames;   // 0 .. N-1
    int closureMaxDiff{-1};             // B1: max of |frame N - frame 0| and |frame N+1 - frame 1|
};
// Renders frames 0 .. N+1 of `spec` on `threads` workers (one frame a worker at a time).
LoopRender renderLoop(const std::string& name, const heldtime::DemoSpec& spec, int threads);
// Frames side by side (same heights).
LoopRender sideBySide(const std::string& name, const std::vector<const LoopRender*>& parts);

struct LoopFiles {
    std::string gif, webp, raw;
    long long gifBytes{0}, webpBytes{0};
    int paletteSize{0}, offPalette{-1};   // B2 on the decoded GIF
    bool gifExact{false};                 // decoded GIF indices equal the encoder's (B4 at scale > 1)
};
// Writes <out>/<name>[-xK].gif and .webp, and <raw>/<name>.rgb (unscaled, for loop_finish.py).
LoopFiles writeLoop(const LoopRender& r, const std::string& out, const std::string& raw, std::int64_t fpsNum, std::int64_t fpsDen,
                    int scale, int paletteColours);

int heldTimeCommand(int argc, char** argv);

}  // namespace raw
