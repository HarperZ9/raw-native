#pragma once
// GPU parity of the screen-space passes (evidence/m3-post-bounds.json): post_gtao and
// post_ssr against raw::post::gtao and raw::post::ssr on the same view-space G-buffer, and
// post_taa against raw::taa::resolve frame by frame on the moving-box sequence, each with a
// control that must fail. Backend-neutral.
#include "raw/rhi/rhi.hpp"
#include "raw/scene/scene.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check {

struct PostSceneCase {
    std::string scene;
    long pixels{0};                    // pixels with geometry
    long gtaoOutside{0};               // |gpu - cpu| > 1e-3
    double gtaoWorst{0};
    long ssrDiffs{0};                  // a different hit pixel, or a hit against a miss
    long ssrHitVsMiss{0}, ssrNeighbour{0};   // of those: one side missed; hits one pixel apart (reported)
    bool pass() const { return pixels > 0 && double(gtaoOutside) <= 1e-3 * double(pixels) && double(ssrDiffs) <= 1e-3 * double(pixels); }
};
struct PostParity {
    std::string error, backend, adapter;
    std::vector<PostSceneCase> scenes;
    int taaFrames{0};
    long taaWorstOutside{0};           // most pixels in one frame with a channel off by more than 1e-3
    double taaWorst{0};
    long taaWorstOneStep{0};           // reported: the most pixels off in one frame when both sides step from the GPU's history
    long taaPixels{0};                 // pixels a frame
    bool gtaoControlFails{false}, ssrControlFails{false}, taaControlFails{false};
    bool pass() const;
    std::string json() const;
};
// Scenes are (name, scene) pairs at size x size; the first scene's last mesh is the caster the
// GTAO and SSR controls move. The TAA check uses the built-in test scene at 128 x 128.
PostParity postParity(rhi::Device& dev, const std::vector<std::pair<std::string, const Scene*>>& scenes, int size = 256);

}  // namespace raw::gpu_check
