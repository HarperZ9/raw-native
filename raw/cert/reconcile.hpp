#pragma once
#include "raw/core/image.hpp"
#include "raw/core/arena.hpp"
#include <cstdint>
namespace raw {
struct ReconcileResult {
    Buffer<float> errorMap;
    float rmse{0}, maxError{0};
    int pixels{0};
    bool withinTolerance{false};
};
ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth,
                          const Buffer<uint8_t>& mask, float tolerance, Arena* arena = nullptr);
}
