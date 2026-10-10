#pragma once
// The HW workstream's Vulkan context (private to src/rhi/vulkan/; bounds in
// evidence/hw-h1-0-vk-bounds.json). One instance, one physical device, one logical device
// with every probed H1 feature enabled that the device offers and RAW_NATIVE_HW_DISABLE does
// not name, one compute queue, and a small compute harness: storage buffers, pipelines from
// SPIR-V with push constants, a timestamp query pool. This is H1's Vulkan measurement path
// and the seed of H3.1's Vulkan backend.
//
// Safety: a device whose type is not CPU is refused unless RAW_NATIVE_VK_HW=1, so a local run
// cannot reach a hardware GPU by accident (the GPU-lock job sets it).
#include "raw/rhi/hw.hpp"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rhi::vk {

struct Buffer { VkBuffer buf{VK_NULL_HANDLE}; VkDeviceMemory mem{VK_NULL_HANDLE}; void* map{nullptr}; uint64_t size{0}; };
struct Pipeline {
    VkDescriptorSetLayout dsl{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
    VkPipeline pipe{VK_NULL_HANDLE};
    uint32_t buffers{0}, pushBytes{0};
};

class Context {
public:
    ~Context();
    // Create the instance, pick the device, probe it, create the logical device.
    bool init(std::string& err);
    const hw::Probe& probe() const { return probe_; }
    uint32_t validationErrors() const { return validationErrors_; }
    std::string firstValidationError() const { return firstError_; }
    uint32_t loaderErrors() const { return loaderErrors_; }
    std::string firstLoaderError() const { return firstLoaderError_; }

    bool buffer(uint64_t size, Buffer& out, std::string& err);   // host-visible, coherent, mapped
    void destroy(Buffer& b);
    // A compute pipeline over `buffers` storage buffers (bindings 0..n-1) and push constants.
    // Returns false with the VkResult when the driver refuses it.
    // accel: binding `buffers` is an acceleration structure (pipeline creation only in H1.0).
    bool pipeline(const uint32_t* spirv, size_t bytes, uint32_t buffers, uint32_t pushBytes, Pipeline& out, std::string& err, bool accel = false);
    void destroy(Pipeline& p);
    // Record one dispatch between two timestamps, submit, wait. ms receives the GPU time.
    bool dispatch(const Pipeline& p, const std::vector<Buffer*>& bufs, const void* push, uint32_t groups, double* ms, std::string& err);
    double timestampPeriodNs() const { return tsPeriod_; }
    bool timestampsUsable() const { return tsBits_ > 0 && tsPeriod_ > 0; }

private:
    VkInstance inst_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
    VkPhysicalDevice phys_{VK_NULL_HANDLE};
    VkDevice dev_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t family_{0};
    VkCommandPool pool_{VK_NULL_HANDLE};
    VkCommandBuffer cmd_{VK_NULL_HANDLE};
    VkFence fence_{VK_NULL_HANDLE};
    VkQueryPool queries_{VK_NULL_HANDLE};
    VkDescriptorPool dpool_{VK_NULL_HANDLE};
    double tsPeriod_{0};
    uint32_t tsBits_{0};
    hw::Probe probe_;
    uint32_t validationErrors_{0};
    std::string firstError_;
    uint32_t loaderErrors_{0};
    std::string firstLoaderError_;
    bool createInstance(std::string& err);
    bool pickDevice(std::string& err);
    bool createDevice(std::string& err);
    static VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                    const VkDebugUtilsMessengerCallbackDataEXT*, void*);
};

// The probe of a physical device (vk_probe.cpp). `extensions` receives the device extensions
// to enable for the features reported supported; `chain` the feature structs to enable.
struct FeatureChain;
hw::Probe probeDevice(VkInstance inst, VkPhysicalDevice phys, std::vector<const char*>& extensions, FeatureChain*& chain);
void freeChain(FeatureChain* chain);
void* chainHead(FeatureChain* chain);
std::string vkError(const char* what, VkResult r);

}  // namespace raw::rhi::vk
