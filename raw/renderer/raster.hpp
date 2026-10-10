#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
namespace raw {
// ids, when given, receives the 1-based index of the mesh nearest at each pixel (0 where
// nothing covers it), for id-edge outlines (raw/renderer/outline.hpp). Without it the
// output is unchanged.
GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena = nullptr, Buffer<uint16_t>* ids = nullptr);
}
