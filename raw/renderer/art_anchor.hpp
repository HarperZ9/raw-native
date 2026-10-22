#pragma once
// P5, world-anchored strokes (evidence/p5-world-strokes-bounds.json): canvas coordinates for the
// painterly shader taken from the world instead of the advected canvas. Each surface pixel gets its
// point's triplanar chart coordinates (by the dominant axis of its normal, each axis with its own
// offset), scaled to canvas pixels by s = f / |eye - target|; a sky pixel gets the cube-map chart
// of its view direction times f. The GPU pass is art_anchor in
// src/renderer/gpu/shaders/art_strokes.wgsl; it mirrors this arithmetic with no trigonometry.
#include "raw/renderer/art_scene.hpp"
#include "raw/renderer/style_paint.hpp"
#include <vector>
namespace raw::art {

struct AnchorParams { double scale{1}, focal{1}; };
AnchorParams anchorParams(const View& v);

// The chart offsets of the y-, x- and z-dominant faces and of the sky, in canvas pixels.
inline constexpr double kAnchorOffset[4][2] = {{0.0, 0.0}, {311.0, 157.0}, {523.0, 419.0}, {1009.0, 787.0}};

// RGBA f32 a pixel: (u, v, 0, 0), the canvas buffer layout of the paint passes' first layer.
std::vector<float> anchorCanvas(const Frame& f, const AnchorParams& a);
// A paint canvas carrying `coords` as its only layer (layer 0 on at full weight, layer 1 off).
style::PaintCanvas worldCanvas(int w, int h, const std::vector<float>& coords);

}  // namespace raw::art
