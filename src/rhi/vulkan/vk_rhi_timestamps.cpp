// GPU timestamps for the Vulkan backend (evidence/gpu-timestamps-bounds.json): a TIMESTAMP query
// pool reset at the start of every list, written at the bottom of the pipe, read after the fence.
#include "vk_rhi.hpp"
#include <algorithm>
namespace raw::rhi::vkr {

VkQueryPool VkrDevice::timestampPool(std::string& err){
    if (tsPool_ != VK_NULL_HANDLE) return tsPool_;
    if (tsChecked_){ err = "vulkan: this queue records no timestamps"; return VK_NULL_HANDLE; }
    tsChecked_ = true;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fam(n);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &n, fam.data());
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_, &props);
    if (family_ >= n || fam[family_].timestampValidBits == 0 || props.limits.timestampPeriod <= 0.0f){ err = "vulkan: this queue records no timestamps"; return VK_NULL_HANDLE; }
    tsPeriodNs_ = double(props.limits.timestampPeriod);
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = kMaxTimestamps;
    const VkResult r = vkCreateQueryPool(dev_, &qi, nullptr, &tsPool_);
    if (r != VK_SUCCESS){ err = vkError("vkCreateQueryPool", r); tsPool_ = VK_NULL_HANDLE; }
    return tsPool_;
}
void VkrDevice::resetTimestamps(VkCommandBuffer cb){
    if (tsPool_ != VK_NULL_HANDLE) vkCmdResetQueryPool(cb, tsPool_, 0, kMaxTimestamps);
}
bool VkrCommandList::timestamp(uint32_t slot){
    if (slot >= kMaxTimestamps){ fail("vulkan: timestamp slot out of range"); return false; }
    std::string err;
    const bool fresh = !dev_.hasTimestampPool();
    const VkQueryPool pool = dev_.timestampPool(err);
    if (pool == VK_NULL_HANDLE) return false;   // reported by timestampSeconds; not a recording error
    if (fresh) dev_.resetTimestamps(cb);        // made inside this list: reset before the first write
    if (rendering) endRenderPass();
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, slot);
    timestampsUsed = std::max(timestampsUsed, slot + 1);
    return true;
}
bool VkrDevice::timestampSeconds(uint32_t count, std::vector<double>& out, std::string& err){
    out.clear();
    if (tsPool_ == VK_NULL_HANDLE){ timestampPool(err); if (err.empty()) err = "vulkan: no timestamps were written"; return false; }
    if (count == 0 || count > tsResolved_){ err = "vulkan: those timestamp slots were not written in the last submission"; return false; }
    std::vector<uint64_t> t(count);
    const VkResult r = vkGetQueryPoolResults(dev_, tsPool_, 0, count, count * 8, t.data(), 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (r != VK_SUCCESS){ err = vkError("vkGetQueryPoolResults", r); return false; }
    for (uint64_t v : t) out.push_back(double(v) * tsPeriodNs_ * 1e-9);
    return true;
}

}  // namespace raw::rhi::vkr
