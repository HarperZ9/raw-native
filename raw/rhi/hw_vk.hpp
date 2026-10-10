#pragma once
// HW H1.0, Vulkan half (evidence/hw-h1-0-vk-bounds.json): the probe and checks through Vulkan,
// built only with RAW_NATIVE_VULKAN=ON into the tool raw_native_vk_probe. The default build has
// no Vulkan code. A device that is not a CPU device is refused unless RAW_NATIVE_VK_HW=1.
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rhi::hw {
struct VkRun { std::string json; int code{0}; };   // code: 0 pass, 1 a bound missed, 4 no device
// checks false: the probe only.
VkRun vulkanProbe(bool checks);
// H1.7 (evidence/hw-h1-7-bounds.json): GEMM on tensor cores and the fallback, at each size (multiples of 32).
VkRun vulkanGemm(const std::vector<uint32_t>& sizes, int warmup, int repeats);
}
