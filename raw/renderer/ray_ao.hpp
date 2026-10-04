#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/renderer/accel.hpp"
#include "raw/core/arena.hpp"
namespace raw {
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius, Arena* arena = nullptr,
                          int threads = 1);
}
