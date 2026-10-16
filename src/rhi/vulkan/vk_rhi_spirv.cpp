// SPIR-V the Vulkan backend hands to the driver: every floating-point add, subtract, multiply and
// dot product is decorated NoContraction, so a driver may not fuse a multiply and an add into one
// rounding (an FMA). This is the Vulkan side of the D3D12 build's dxc -Gis (strict IEEE): the
// renderer's checks compare GPU passes with CPU references that round each operation, and some
// (the software rasterizer, shadow-map depth) are bit-exact. The decoration changes no operation's
// value; it only forbids merging two of them. See vk_rhi.hpp.
#include "vk_rhi.hpp"
#include <cstddef>
#include <vector>
namespace raw::rhi::vkr {
namespace {
constexpr uint32_t kOpDecorate = 71, kNoContraction = 42;
bool contractible(uint32_t op){
    switch (op){
        case 129: case 131: case 133:          // OpFAdd, OpFSub, OpFMul
        case 142: case 143: case 144: case 145: case 146:   // vector/matrix times scalar/vector/matrix
        case 148:                              // OpDot
            return true;
        default: return false;
    }
}
}  // namespace

// Returns the decorated module, or the input unchanged when it is not a SPIR-V module this pass
// understands (a bad magic number or a truncated instruction).
std::vector<uint32_t> noContraction(const uint32_t* words, size_t count){
    std::vector<uint32_t> in(words, words + count);
    if (count < 5 || in[0] != 0x07230203u) return in;
    std::vector<uint32_t> ids, already;
    size_t firstType = 0;
    for (size_t i = 5; i < count;){
        const uint32_t wc = in[i] >> 16, op = in[i] & 0xFFFF;
        if (wc == 0 || i + wc > count) return in;
        if (!firstType && op >= 19 && op <= 39) firstType = i;   // OpTypeVoid .. OpTypeForwardPointer: annotations end here
        if (op == kOpDecorate && wc >= 3 && in[i + 2] == kNoContraction) already.push_back(in[i + 1]);
        if (contractible(op) && wc >= 3) ids.push_back(in[i + 2]);   // result id follows the result type
        i += wc;
    }
    if (!firstType || ids.empty()) return in;
    std::vector<uint32_t> out(in.begin(), in.begin() + (long)firstType);
    for (uint32_t id : ids){
        bool dup = false;
        for (uint32_t a : already) dup = dup || a == id;
        if (dup) continue;
        out.push_back((3u << 16) | kOpDecorate); out.push_back(id); out.push_back(kNoContraction);
    }
    out.insert(out.end(), in.begin() + (long)firstType, in.end());
    return out;
}
}  // namespace raw::rhi::vkr
