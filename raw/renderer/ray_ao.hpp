#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/renderer/accel.hpp"
#include "raw/core/arena.hpp"
namespace raw {
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius, Arena* arena = nullptr,
                          int threads = 1);
// The AO reference and lighting model 2's visibility from the same rays: ao exactly as
// computeRTAO; sky, the fraction of the samples with no hit at any distance; sun, 1 when a ray
// towards `toLight` hits nothing at any distance (forced to 1 when sunForced). 1 off the mask.
void computeRTVisibility(const GBuffer& g, const LinearAccel& accel, int samples, float radius, Vec3 toLight,
                         Buffer<float>& ao, Buffer<float>& sky, Buffer<float>& sun, int threads = 1,
                         bool sunForced = false);
}
