#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
namespace raw { GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena = nullptr); }
