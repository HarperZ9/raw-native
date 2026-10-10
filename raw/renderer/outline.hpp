#pragma once
// Outlines and toon ramps (ROADMAP M2 criterion 11), on the CPU G-buffer.
//
//   edges(g, ids, Edge::Depth)    the nearer pixel of each depth discontinuity
//   edges(g, ids, Edge::Id)       the nearer pixel where the object id changes (needs ids)
//   edges(g, ids, Edge::Normal)   both pixels of a crease between covered pixels
//   invertedHull(scene, w, h, t)  pixels where the back faces of each mesh pushed out by t
//                                 along its normals show past the mesh: the hull outline
//   toonBands(g, light, n)        the toon ramp's band per covered pixel, -1 elsewhere
//
// The checks against analytic references are tests/test_outlines.cpp, with the bounds in
// evidence/m2-outlines-bounds.json. The GPU twins come with the M2 render passes.
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
#include <vector>
namespace raw::outline {

enum class Edge { Depth, Normal, Id };
struct Params {
    float depthRel = 0.05f;   // relative depth step that counts as an edge
    float normalCos = 0.8f;   // neighbours whose normals' cosine falls below this form a crease
};

std::vector<uint8_t> edges(const GBuffer& g, const Buffer<uint16_t>* ids, Edge kind, const Params& p = {});
std::vector<uint8_t> invertedHull(const Scene& scene, int w, int h, float thickness);
std::vector<int8_t> toonBands(const GBuffer& g, Vec3 lightDir, int bands);

}  // namespace raw::outline
