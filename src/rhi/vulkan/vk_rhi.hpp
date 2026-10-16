#pragma once
// The Vulkan backend of the RHI (N3, evidence/n3-vulkan-raster-bounds.json). Private to
// src/rhi/vulkan/: nothing outside this directory includes a Vulkan header. Separate from the HW
// workstream's probe context (vk_context.hpp), which measures features and stays a tool.
//
// Device: Vulkan 1.3, one graphics queue, dynamic rendering. A device whose type is not CPU
// (lavapipe, SwiftShader) is refused unless RAW_NATIVE_VK_HW=1, so a local run cannot reach a
// hardware GPU by accident. RAW_NATIVE_VK_VALIDATION=1 enables the Khronos validation layer, and
// any error it reports fails the submission (as RAW_NATIVE_D3D12_DEBUG does on D3D12).
// Memory: one allocation a resource. Uniform and MapRead buffers are host-visible and stay mapped;
// other buffers are device-local, zero-filled at creation and filled through staging copies.
// Binding: binding i of a pipeline is descriptor binding i of set 0, as the WGSL declares it and as
// glslang's register shifts place it (uniform b0 -> 0, texture t0 and storage u0 -> 1, ...).
// Textures track their own image layout; a draw that samples a texture moves it to
// SHADER_READ_ONLY before rendering begins, restarting the rendering with LOAD when needed.
// The viewport is flipped (negative height) so clip space is y-up, as on D3D12 and WebGPU, and
// front faces are counter-clockwise there.
#include "raw/rhi/rhi.hpp"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rhi::vkr {

std::string vkError(const std::string& what, VkResult r);

// A generational table like raw::SlotPool, which the device can also walk to release what is left.
template<class T> class Table {
public:
    struct Id { uint32_t index{0}, gen{0}; };
    Id insert(T v){
        uint32_t i;
        if (!free_.empty()){ i = free_.back(); free_.pop_back(); } else { i = (uint32_t)slots_.size(); slots_.push_back({}); }
        slots_[i].value = std::move(v); slots_[i].live = true;
        return {i, slots_[i].gen};
    }
    T* get(uint32_t i, uint32_t g){ return i < slots_.size() && g && slots_[i].gen == g && slots_[i].live ? &slots_[i].value : nullptr; }
    bool erase(uint32_t i, uint32_t g, T& out){
        if (!get(i, g)) return false;
        out = std::move(slots_[i].value); slots_[i].value = T{}; slots_[i].live = false;
        if (++slots_[i].gen == 0) slots_[i].gen = 1;
        free_.push_back(i);
        return true;
    }
    template<class F> void forEach(F f){ for (Slot& s : slots_) if (s.live) f(s.value); }
private:
    struct Slot { T value{}; uint32_t gen{1}; bool live{false}; };
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
};
VkFormat vkFormat(TextureFormat f);
// Every float add, subtract, multiply and dot product decorated NoContraction (vk_rhi_spirv.cpp).
std::vector<uint32_t> noContraction(const uint32_t* words, size_t count);

struct BufferRec { VkBuffer buf{VK_NULL_HANDLE}; VkDeviceMemory mem{VK_NULL_HANDLE}; void* map{nullptr}; uint64_t size{0}; bool host{false}; };
struct TexRec {
    VkImage img{VK_NULL_HANDLE}; VkDeviceMemory mem{VK_NULL_HANDLE}; VkImageView view{VK_NULL_HANDLE};
    uint32_t w{0}, h{0}; TextureFormat format{TextureFormat::RGBA8Unorm}; TextureUsage usage{TextureUsage::None};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED}; bool owned{true};   // false: a swapchain image
};
struct SampRec { VkSampler s{VK_NULL_HANDLE}; };
struct PipeRec {
    VkDescriptorSetLayout dsl{VK_NULL_HANDLE}; VkPipelineLayout layout{VK_NULL_HANDLE}; VkPipeline pipe{VK_NULL_HANDLE};
    std::vector<VkDescriptorType> types;
    uint32_t targetCount{0}; bool depth{false};   // raster pipelines
};

class VkrDevice;
class VkrCommandList final : public CommandList {
public:
    explicit VkrCommandList(VkrDevice& d) : dev_(d) {}
    void barrier(std::span<const BufferBarrier> barriers) override;
    void upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size) override;
    void copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z) override;
    void uploadTexture(TextureHandle dst, const void* data, uint32_t width, uint32_t height) override;
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst) override;
    void beginRenderPass(const RenderPassDesc& pass) override;
    void draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount) override;
    void endRenderPass() override;
    // Move a texture to `to`, recording the barrier when it is not there already.
    void transition(TexRec& t, VkImageLayout to);
    void reset(){ error.clear(); staging.clear(); open = false; rendering = false; cleared_ = false; pass = {}; }
    std::string error;
    std::vector<BufferRec> staging;   // freed after the submission completes
    VkCommandBuffer cb{VK_NULL_HANDLE};
private:
    void fail(const std::string& e){ if (error.empty()) error = e; }
    void startRendering(bool load);
    VkDescriptorSet descriptors(const PipeRec& p, std::span<const RasterBind> rb, std::span<const BufferHandle> bb);
    VkrDevice& dev_;
    RenderPassDesc pass;
    bool open{false}, rendering{false}, cleared_{false};
};

class VkrDevice final : public Device {
public:
    ~VkrDevice() override;
    bool init(std::string& err);
    const char* backendName() const override { return "vulkan"; }
    const AdapterInfo& adapter() const override { return info_; }
    ShaderFormat shaderFormat() const override { return ShaderFormat::Spirv; }
    BufferHandle createBuffer(const BufferDesc& desc, std::string& err) override;
    void destroyBuffer(BufferHandle buffer) override;
    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc, std::string& err) override;
    TextureHandle createTexture(const TextureDesc& desc, std::string& err) override;
    void destroyTexture(TextureHandle texture) override;
    SamplerHandle createSampler(const SamplerDesc& desc, std::string& err) override;
    RasterPipelineHandle createRasterPipeline(const RasterPipelineDesc& desc, std::string& err) override;
    CommandList* begin(std::string& err) override;
    bool submitAndWait(std::string& err) override;
    const void* mapRead(BufferHandle buffer, uint64_t size, std::string& err) override;
    void unmap(BufferHandle) override {}
    std::unique_ptr<Swapchain> createSwapchain(const SwapchainDesc& desc, std::string& err) override;
    bool lost() const override { return lost_; }
    bool loseForTest(std::string& err) override;

    BufferRec* buffer(BufferHandle h){ return buffers_.get(h.index, h.gen); }
    TexRec* texture(TextureHandle h){ return textures_.get(h.index, h.gen); }
    SampRec* sampler(SamplerHandle h){ return samplers_.get(h.index, h.gen); }
    PipeRec* pipeline(PipelineHandle h){ return pipes_.get(h.index, h.gen); }
    PipeRec* raster(RasterPipelineHandle h){ return rasters_.get(h.index, h.gen); }
    TextureHandle adoptTexture(TexRec t){ auto id = textures_.insert(std::move(t)); return {id.index, id.gen}; }
    VkDevice vk() const { return dev_; }
    VkInstance instance() const { return inst_; }
    VkPhysicalDevice physical() const { return phys_; }
    VkQueue queue() const { return queue_; }
    uint32_t family() const { return family_; }
    VkDescriptorPool descriptorPool() const { return dpool_; }
    bool hasInstanceExtension(const char* name) const;
    // VK_GOOGLE_display_timing was offered and enabled (the refresh cycle of a swapchain's output).
    bool hasDisplayTiming() const { return displayTiming_; }
    // A buffer outside the slot table (staging), host-visible and mapped when `host`.
    bool makeBuffer(uint64_t size, VkBufferUsageFlags usage, bool host, BufferRec& out, std::string& err);
    void freeBuffer(BufferRec& b);
    bool memoryType(uint32_t bits, VkMemoryPropertyFlags want, uint32_t& out) const;
    void markLost(){ lost_ = true; }
    void waitIdle(){ if (dev_) vkDeviceWaitIdle(dev_); }

private:
    bool createInstance(std::string& err);
    bool pickDevice(std::string& err);
    bool createLogical(std::string& err);
    bool zeroFill(BufferRec& b, std::string& err);
    bool validationErrors(std::string& err);
    VkInstance inst_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
    VkPhysicalDevice phys_{VK_NULL_HANDLE};
    VkDevice dev_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t family_{0};
    VkCommandPool pool_{VK_NULL_HANDLE};
    VkFence fence_{VK_NULL_HANDLE};
    VkDescriptorPool dpool_{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memProps_{};
    std::vector<std::string> instanceExtensions_;
    AdapterInfo info_;
    Table<BufferRec> buffers_;
    Table<TexRec> textures_;
    Table<SampRec> samplers_;
    Table<PipeRec> pipes_, rasters_;
    void release(TexRec& t);
    void release(PipeRec& p);
    VkrCommandList cmd_{*this};
    bool lost_{false}, injectedLoss_{false}, displayTiming_{false};
public:
    // Validation messages counted by the messenger (RAW_NATIVE_VK_VALIDATION=1).
    uint32_t validationCount{0};
    std::string firstValidation;
};
}  // namespace raw::rhi::vkr
