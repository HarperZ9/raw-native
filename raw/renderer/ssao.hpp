#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/core/arena.hpp"
namespace raw {
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius, Arena* arena = nullptr,
                          int threads = 1);
}
