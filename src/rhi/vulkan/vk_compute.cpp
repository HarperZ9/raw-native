// The HW Vulkan compute harness: buffers, pipelines and a timed dispatch (vk_context.hpp).
#include "vk_context.hpp"
#include <cstdlib>
#include <cstring>
namespace raw::rhi::vk {

bool Context::allocate(uint64_t size, VkMemoryPropertyFlags want, Buffer& out, std::string& err){
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(dev_, &bi, nullptr, &out.buf);
    if (r != VK_SUCCESS){ err = vkError("vkCreateBuffer", r); return false; }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev_, out.buf, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount && type == UINT32_MAX; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) type = i;
    if (type == UINT32_MAX){ err = "no memory type with the wanted properties"; return false; }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size; ai.memoryTypeIndex = type;
    if ((r = vkAllocateMemory(dev_, &ai, nullptr, &out.mem)) != VK_SUCCESS){ err = vkError("vkAllocateMemory", r); return false; }
    if ((r = vkBindBufferMemory(dev_, out.buf, out.mem, 0)) != VK_SUCCESS){ err = vkError("vkBindBufferMemory", r); return false; }
    out.size = size;
    return true;
}
bool Context::buffer(uint64_t size, Buffer& out, std::string& err){
    if (!allocate(size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, out, err)) return false;
    VkResult r = vkMapMemory(dev_, out.mem, 0, VK_WHOLE_SIZE, 0, &out.map);
    if (r != VK_SUCCESS){ err = vkError("vkMapMemory", r); return false; }
    std::memset(out.map, 0, size);
    return true;
}
bool Context::deviceBuffer(uint64_t size, Buffer& out, std::string& err){
    return allocate(size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, out, err);
}
bool Context::copy(VkBuffer dst, VkBuffer src, uint64_t size, std::string& err){
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(cmd_, 0);
    vkBeginCommandBuffer(cmd_, &bi);
    const VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd_, src, dst, 1, &region);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
    VkResult r = vkEndCommandBuffer(cmd_);
    if (r != VK_SUCCESS){ err = vkError("vkEndCommandBuffer", r); return false; }
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.commandBufferCount = 1; s.pCommandBuffers = &cmd_;
    vkResetFences(dev_, 1, &fence_);
    if ((r = vkQueueSubmit(queue_, 1, &s, fence_)) != VK_SUCCESS){ err = vkError("vkQueueSubmit", r); return false; }
    if ((r = vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX)) != VK_SUCCESS){ err = vkError("vkWaitForFences", r); return false; }
    return true;
}
bool Context::upload(Buffer& dst, const void* data, uint64_t size, std::string& err){
    Buffer st;
    const bool ok = buffer(size, st, err) && (std::memcpy(st.map, data, size), copy(dst.buf, st.buf, size, err));
    destroy(st);
    return ok;
}
bool Context::download(Buffer& src, void* data, uint64_t size, std::string& err){
    Buffer st;
    const bool ok = buffer(size, st, err) && copy(st.buf, src.buf, size, err);
    if (ok) std::memcpy(data, st.map, size);
    destroy(st);
    return ok;
}
void Context::destroy(Buffer& b){
    if (b.mem){ if (b.map) vkUnmapMemory(dev_, b.mem); vkFreeMemory(dev_, b.mem, nullptr); }
    if (b.buf) vkDestroyBuffer(dev_, b.buf, nullptr);
    b = {};
}

bool Context::pipeline(const uint32_t* spirv, size_t bytes, uint32_t buffers, uint32_t pushBytes, Pipeline& out, std::string& err, bool accel){
    std::vector<VkDescriptorSetLayoutBinding> b(buffers);
    for (uint32_t i = 0; i < buffers; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    if (accel) b.push_back({buffers, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = (uint32_t)b.size(); li.pBindings = b.data();
    VkResult r = vkCreateDescriptorSetLayout(dev_, &li, nullptr, &out.dsl);
    if (r != VK_SUCCESS){ err = vkError("vkCreateDescriptorSetLayout", r); return false; }
    VkPushConstantRange pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &out.dsl;
    if (pushBytes){ pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr; }
    if ((r = vkCreatePipelineLayout(dev_, &pl, nullptr, &out.layout)) != VK_SUCCESS){ err = vkError("vkCreatePipelineLayout", r); return false; }
    VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = bytes; si.pCode = spirv;
    VkShaderModule mod{};
    if ((r = vkCreateShaderModule(dev_, &si, nullptr, &mod)) != VK_SUCCESS){ err = vkError("vkCreateShaderModule", r); return false; }
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr};
    ci.layout = out.layout;
    r = vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &ci, nullptr, &out.pipe);
    vkDestroyShaderModule(dev_, mod, nullptr);
    if (r != VK_SUCCESS){ err = vkError("vkCreateComputePipelines", r); return false; }
    out.buffers = buffers; out.pushBytes = pushBytes;
    return true;
}
void Context::destroy(Pipeline& p){
    if (p.pipe) vkDestroyPipeline(dev_, p.pipe, nullptr);
    if (p.layout) vkDestroyPipelineLayout(dev_, p.layout, nullptr);
    if (p.dsl) vkDestroyDescriptorSetLayout(dev_, p.dsl, nullptr);
    p = {};
}

bool Context::dispatch(const Pipeline& p, const std::vector<Buffer*>& bufs, const void* push, uint32_t groups, double* ms, std::string& err,
                       uint32_t groupsY){
    vkResetDescriptorPool(dev_, dpool_, 0);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dpool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &p.dsl;
    VkDescriptorSet set{};
    VkResult r = vkAllocateDescriptorSets(dev_, &ai, &set);
    if (r != VK_SUCCESS){ err = vkError("vkAllocateDescriptorSets", r); return false; }
    std::vector<VkDescriptorBufferInfo> infos(p.buffers);
    std::vector<VkWriteDescriptorSet> writes(p.buffers);
    for (uint32_t i = 0; i < p.buffers; ++i){
        infos[i] = {bufs[i]->buf, 0, VK_WHOLE_SIZE};
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &infos[i], nullptr};
    }
    vkUpdateDescriptorSets(dev_, p.buffers, writes.data(), 0, nullptr);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(cmd_, 0);
    vkBeginCommandBuffer(cmd_, &bi);
    const bool timed = ms && queries_;
    if (timed){
        vkCmdResetQueryPool(cmd_, queries_, 0, 2);
        vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries_, 0);
    }
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipe);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &set, 0, nullptr);
    // RAW_NATIVE_VK_MISUSE=1: the validation control (bounds, method note 2) writes 4 bytes past the range.
    static const bool misuse = std::getenv("RAW_NATIVE_VK_MISUSE") && std::strcmp(std::getenv("RAW_NATIVE_VK_MISUSE"), "1") == 0;
    const uint32_t pad[8]{};
    if (p.pushBytes) vkCmdPushConstants(cmd_, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, p.pushBytes + (misuse ? 4u : 0u), misuse ? pad : push);
    vkCmdDispatch(cmd_, groups, groupsY, 1);
    if (timed) vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, 1);
    // Shader writes become visible to the host and to a later copy (device-local read-back).
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
    if ((r = vkEndCommandBuffer(cmd_)) != VK_SUCCESS){ err = vkError("vkEndCommandBuffer", r); return false; }
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.commandBufferCount = 1; s.pCommandBuffers = &cmd_;
    vkResetFences(dev_, 1, &fence_);
    if ((r = vkQueueSubmit(queue_, 1, &s, fence_)) != VK_SUCCESS){ err = vkError("vkQueueSubmit", r); return false; }
    if ((r = vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX)) != VK_SUCCESS){ err = vkError("vkWaitForFences", r); return false; }
    if (timed){
        uint64_t t[2]{};
        if ((r = vkGetQueryPoolResults(dev_, queries_, 0, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT)) != VK_SUCCESS){
            err = vkError("vkGetQueryPoolResults", r); return false; }
        const uint64_t mask = tsBits_ >= 64 ? ~0ull : ((1ull << tsBits_) - 1);
        *ms = (double)((t[1] - t[0]) & mask) * tsPeriod_ * 1e-6;
    }
    return true;
}

}  // namespace raw::rhi::vk
