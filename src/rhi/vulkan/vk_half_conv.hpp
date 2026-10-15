// fp32 to fp16 and back on the host, for the half weight tables of the cooperative-matrix kernels
// (vk_gemm.cpp, vk_rrnet.cpp). Round to nearest even; values past fp16's range become infinity.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
namespace raw::rhi::hw {
inline uint16_t toHalf(float f){
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (e <= 0){
        if (e < -10) return (uint16_t)sign;
        m |= 0x800000u;
        const uint32_t shift = (uint32_t)(14 - e), r = m >> shift, rem = m & ((1u << shift) - 1), half = 1u << (shift - 1);
        return (uint16_t)(sign | (r + (rem > half || (rem == half && (r & 1u)))));
    }
    if (e >= 31) return (uint16_t)(sign | 0x7C00u);
    uint32_t h = sign | ((uint32_t)e << 10) | (m >> 13);
    const uint32_t rem = m & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
    return (uint16_t)h;
}
inline float fromHalf(uint16_t h){
    const uint32_t sign = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    if (e == 0) return (sign ? -1.0f : 1.0f) * std::ldexp((float)m, -24);
    if (e == 31) return (sign ? -1.0f : 1.0f) * (m ? NAN : INFINITY);
    const uint32_t x = sign | ((e + 112u) << 23) | (m << 13);
    float f; std::memcpy(&f, &x, 4); return f;
}
}  // namespace raw::rhi::hw
