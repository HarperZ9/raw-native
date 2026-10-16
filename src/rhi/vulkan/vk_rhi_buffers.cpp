// The Vulkan RHI's buffers: memory types, staging, zero fill and read-back (see vk_rhi.hpp).
#include "vk_rhi.hpp"
#include <cstring>
namespace raw::rhi::vkr {
bool VkrDevice::memoryType(uint32_t bits, VkMemoryPropertyFlags want, uint32_t& out) const {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & want) == want){ out = i; return true; }
    return false;
}
bool VkrDevice::makeBuffer(uint64_t size, VkBufferUsageFlags usage, bool host, BufferRec& out, std::string& err){
    out = {};
    out.size = size; out.host = host;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size < 16 ? 16 : (size + 15) & ~uint64_t(15); bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(dev_, &bi, nullptr, &out.buf);
    if (r != VK_SUCCESS){ err = vkError("vkCreateBuffer (" + std::to_string(size) + " bytes)", r); return false; }
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(dev_, out.buf, &mr);
    uint32_t type = 0;
    const bool found = host ? memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, type)
                            : (memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type) || memoryType(mr.memoryTypeBits, 0, type));
    if (!found){ err = "vulkan: no memory type for a buffer"; freeBuffer(out); return false; }
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size; ma.memoryTypeIndex = type;
    if ((r = vkAllocateMemory(dev_, &ma, nullptr, &out.mem)) != VK_SUCCESS){ err = vkError("vkAllocateMemory", r); freeBuffer(out); return false; }
    vkBindBufferMemory(dev_, out.buf, out.mem, 0);
    if (host && (r = vkMapMemory(dev_, out.mem, 0, VK_WHOLE_SIZE, 0, &out.map)) != VK_SUCCESS){ err = vkError("vkMapMemory", r); freeBuffer(out); return false; }
    return true;
}
void VkrDevice::freeBuffer(BufferRec& b){
    if (b.buf) vkDestroyBuffer(dev_, b.buf, nullptr);
    if (b.mem) vkFreeMemory(dev_, b.mem, nullptr);
    b = {};
}
// Device-local buffers start zero-filled, as on every backend: one fill in its own submission.
bool VkrDevice::zeroFill(BufferRec& b, std::string& err){
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkResult r = vkAllocateCommandBuffers(dev_, &ai, &cb);
    if (r != VK_SUCCESS){ err = vkError("vkAllocateCommandBuffers (fill)", r); return false; }
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    vkCmdFillBuffer(cb, b.buf, 0, VK_WHOLE_SIZE, 0);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    r = vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    if (r == VK_SUCCESS) r = vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(dev_, pool_, 1, &cb);
    if (r != VK_SUCCESS){ if (r == VK_ERROR_DEVICE_LOST) lost_ = true; err = vkError("buffer zero fill", r); return false; }
    return true;
}
BufferHandle VkrDevice::createBuffer(const BufferDesc& desc, std::string& err){
    const bool host = has(desc.usage, BufferUsage::Uniform) || has(desc.usage, BufferUsage::MapRead);
    VkBufferUsageFlags u = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (has(desc.usage, BufferUsage::Uniform)) u |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (has(desc.usage, BufferUsage::Storage)) u |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferRec b;
    if (!makeBuffer(desc.size, u, host, b, err)) return {};
    if (host) std::memset(b.map, 0, (size_t)desc.size);
    else if (!zeroFill(b, err)){ freeBuffer(b); return {}; }
    auto id = buffers_.insert(b);
    return {id.index, id.gen};
}
void VkrDevice::destroyBuffer(BufferHandle h){
    BufferRec b;
    if (buffers_.erase(h.index, h.gen, b)) freeBuffer(b);
}

const void* VkrDevice::mapRead(BufferHandle h, uint64_t size, std::string& err){
    BufferRec* b = buffer(h);
    if (!b || !b->host || !b->map){ err = "vulkan: mapRead needs a MapRead buffer"; return nullptr; }
    if (size > b->size){ err = "vulkan: mapRead past the end of a buffer"; return nullptr; }
    return b->map;
}
}  // namespace raw::rhi::vkr
