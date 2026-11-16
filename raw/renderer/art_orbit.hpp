#pragma once
// The stylised track's shared camera sequence (P5's orbit, evidence/p5-world-strokes-bounds.json):
// the art scene's frames along the orbit with their motion vectors and history masks, and the
// warp-metric series of a pass's outputs against a reference over those frames.
#include "raw/renderer/art_metrics.hpp"
#include "raw/renderer/art_scene.hpp"
#include <cstdint>
#include <vector>
namespace raw::art {

struct OrbitSeq {
    std::vector<View> views;
    std::vector<Frame> frames;
    std::vector<std::vector<float>> mv;               // frame 0: zeros
    std::vector<std::vector<std::uint8_t>> valid;     // frame 0: empty
};
// `frames` frames of the orbit at w x h; step 0 holds the camera still (a static sequence).
OrbitSeq renderOrbitSeq(int w, int h, int frames, double step, const Lighting& L, int threads);
// Instability of frames first .. n - 1 of `out` against `ref` (RGBA codes a frame).
std::vector<double> instabilitySeries(const OrbitSeq& s, const std::vector<std::vector<std::uint8_t>>& out,
                                      const std::vector<std::vector<std::uint8_t>>& ref, int first);

}  // namespace raw::art
