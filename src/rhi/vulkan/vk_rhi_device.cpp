// The Vulkan RHI device: instance, device choice, buffers, submission and read-back (see vk_rhi.hpp).
#include "vk_rhi.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace raw::rhi::vkr {
namespace {
bool envOn(const char* n){ const char* v = std::getenv(n); return v && std::strcmp(v, "1") == 0; }
VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                                         const VkDebugUtilsMessengerCallbackDataEXT* d, void* user){
    auto* dev = static_cast<VkrDevice*>(user);
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){
        if (dev->validationCount++ == 0 && d && d->pMessage) dev->firstValidation = d->pMessage;
    }
    return VK_FALSE;
}
const char* typeName(VkPhysicalDeviceType t){
    switch (t){ case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu"; case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated"; case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual"; default: return "other"; }
}
}  // namespace

std::string vkError(const std::string& what, VkResult r){ return what + " failed: VkResult " + std::to_string((int)r); }

VkrDevice::~VkrDevice(){
    if (dev_){
        vkDeviceWaitIdle(dev_);
        for (BufferRec& b : cmd_.staging) freeBuffer(b);
        cmd_.staging.clear();
        buffers_.forEach([&](BufferRec& b){ freeBuffer(b); });
        textures_.forEach([&](TexRec& t){ release(t); });
        samplers_.forEach([&](SampRec& x){ vkDestroySampler(dev_, x.s, nullptr); });
        pipes_.forEach([&](PipeRec& p){ release(p); });
        rasters_.forEach([&](PipeRec& p){ release(p); });
        if (dpool_) vkDestroyDescriptorPool(dev_, dpool_, nullptr);
        if (fence_) vkDestroyFence(dev_, fence_, nullptr);
        if (pool_) vkDestroyCommandPool(dev_, pool_, nullptr);
        vkDestroyDevice(dev_, nullptr);
    }
    if (messenger_){
        auto f = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst_, "vkDestroyDebugUtilsMessengerEXT");
        if (f) f(inst_, messenger_, nullptr);
    }
    if (inst_) vkDestroyInstance(inst_, nullptr);
}

bool VkrDevice::hasInstanceExtension(const char* name) const {
    for (const std::string& e : instanceExtensions_) if (e == name) return true;
    return false;
}

bool VkrDevice::createInstance(std::string& err){
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> avail(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, avail.data());
    std::vector<const char*> ext;
    for (const char* want : {"VK_KHR_surface", "VK_KHR_win32_surface", "VK_KHR_xcb_surface", "VK_KHR_wayland_surface",
                             "VK_EXT_swapchain_colorspace", "VK_KHR_get_surface_capabilities2"}){
        for (const auto& a : avail) if (std::strcmp(a.extensionName, want) == 0){ ext.push_back(want); instanceExtensions_.push_back(want); }
    }
    const bool validation = envOn("RAW_NATIVE_VK_VALIDATION");
    std::vector<const char*> layers;
    if (validation){
        ext.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "raw-native"; app.pEngineName = "raw-native"; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)ext.size(); ci.ppEnabledExtensionNames = ext.data();
    ci.enabledLayerCount = (uint32_t)layers.size(); ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &inst_);
    if (r != VK_SUCCESS){
        err = vkError("vkCreateInstance", r) + (validation ? " (RAW_NATIVE_VK_VALIDATION=1 needs the Khronos validation layer)" : "");
        return false;
    }
    if (validation){
        VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        m.pfnUserCallback = onMessage; m.pUserData = this;
        auto f = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst_, "vkCreateDebugUtilsMessengerEXT");
        if (!f || f(inst_, &m, nullptr, &messenger_) != VK_SUCCESS){ err = "RAW_NATIVE_VK_VALIDATION=1 but the debug messenger was not created"; return false; }
    }
    return true;
}

// The first CPU device (a software driver), or with RAW_NATIVE_VK_HW=1 the first device of any
// type; either way it must offer Vulkan 1.3 with dynamic rendering and a graphics queue.
bool VkrDevice::pickDevice(std::string& err){
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst_, &n, devs.data());
    const bool hw = envOn("RAW_NATIVE_VK_HW");
    std::string skipped;
    for (VkPhysicalDevice d : devs){
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        const std::string name = p.deviceName;
        if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && !hw){ skipped += (skipped.empty() ? "" : ", ") + name + " (" + typeName(p.deviceType) + ", needs RAW_NATIVE_VK_HW=1)"; continue; }
        if (p.apiVersion < VK_API_VERSION_1_3){ skipped += (skipped.empty() ? "" : ", ") + name + " (below Vulkan 1.3)"; continue; }
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(d, &f2);
        if (!f13.dynamicRendering){ skipped += (skipped.empty() ? "" : ", ") + name + " (no dynamic rendering)"; continue; }
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> q(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, q.data());
        for (uint32_t i = 0; i < qn; ++i){
            if (!(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !(q[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
            phys_ = d; family_ = i;
            char id[16]; std::snprintf(id, sizeof id, "0x%04X", p.deviceID);
            char drv[32]; std::snprintf(drv, sizeof drv, "%u.%u.%u", VK_API_VERSION_MAJOR(p.driverVersion), VK_API_VERSION_MINOR(p.driverVersion), VK_API_VERSION_PATCH(p.driverVersion));
            char api[32]; std::snprintf(api, sizeof api, "vulkan %u.%u.%u", VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion));
            info_ = AdapterInfo{p.vendorID == 0x10005 ? "mesa" : p.vendorID == 0x1AE0 ? "google" : std::to_string(p.vendorID), typeName(p.deviceType), id, name, api, drv};
            vkGetPhysicalDeviceMemoryProperties(d, &memProps_);
            return true;
        }
        skipped += (skipped.empty() ? "" : ", ") + name + " (no graphics and compute queue)";
    }
    err = "no usable Vulkan device" + (skipped.empty() ? std::string(" (no device listed; set VK_DRIVER_FILES to a software driver)") : "; skipped: " + skipped);
    return false;
}

bool VkrDevice::createLogical(std::string& err){
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> avail(n);
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &n, avail.data());
    std::vector<const char*> ext;
    for (const auto& a : avail) if (std::strcmp(a.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) ext.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    VkPhysicalDeviceFeatures have{};
    vkGetPhysicalDeviceFeatures(phys_, &have);
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext = &f13;
    // Storage buffers read by vertex and fragment shaders (vertex pulling, the PQ pass).
    f2.features.vertexPipelineStoresAndAtomics = have.vertexPipelineStoresAndAtomics;
    f2.features.fragmentStoresAndAtomics = have.fragmentStoresAndAtomics;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family_; q.queueCount = 1; q.pQueuePriorities = &prio;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f2; ci.queueCreateInfoCount = 1; ci.pQueueCreateInfos = &q;
    ci.enabledExtensionCount = (uint32_t)ext.size(); ci.ppEnabledExtensionNames = ext.data();
    VkResult r = vkCreateDevice(phys_, &ci, nullptr, &dev_);
    if (r != VK_SUCCESS){ err = vkError("vkCreateDevice", r); return false; }
    vkGetDeviceQueue(dev_, family_, 0, &queue_);
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pc.queueFamilyIndex = family_;
    if ((r = vkCreateCommandPool(dev_, &pc, nullptr, &pool_)) != VK_SUCCESS){ err = vkError("vkCreateCommandPool", r); return false; }
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    if ((r = vkAllocateCommandBuffers(dev_, &ai, &cmd_.cb)) != VK_SUCCESS){ err = vkError("vkAllocateCommandBuffers", r); return false; }
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if ((r = vkCreateFence(dev_, &fc, nullptr, &fence_)) != VK_SUCCESS){ err = vkError("vkCreateFence", r); return false; }
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192},
                                          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1024}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 4096; dp.poolSizeCount = 4; dp.pPoolSizes = sizes;
    if ((r = vkCreateDescriptorPool(dev_, &dp, nullptr, &dpool_)) != VK_SUCCESS){ err = vkError("vkCreateDescriptorPool", r); return false; }
    return true;
}

bool VkrDevice::init(std::string& err){
    return createInstance(err) && pickDevice(err) && createLogical(err);
}

CommandList* VkrDevice::begin(std::string& err){
    if (cmd_.cb == VK_NULL_HANDLE){ err = "vulkan: no command buffer"; return nullptr; }
    if (!cmd_.staging.empty()){ vkQueueWaitIdle(queue_); for (BufferRec& b : cmd_.staging) freeBuffer(b); }   // left by a failed submission
    cmd_.reset();
    vkResetDescriptorPool(dev_, dpool_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult r = vkResetCommandBuffer(cmd_.cb, 0);
    if (r == VK_SUCCESS) r = vkBeginCommandBuffer(cmd_.cb, &bi);
    if (r != VK_SUCCESS){ err = vkError("vkBeginCommandBuffer", r); return nullptr; }
    return &cmd_;
}
bool VkrDevice::validationErrors(std::string& err){
    if (validationCount == 0) return true;
    err = "the Vulkan validation layer reported " + std::to_string(validationCount) + " error(s); first: " + firstValidation;
    validationCount = 0;
    return false;
}
bool VkrDevice::submitAndWait(std::string& err){
    cmd_.endRenderPass();   // a pass left open by a failed recording must not leave rendering active
    // Everything the list wrote is visible to the host afterwards (read-back buffers).
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd_.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    VkResult r = vkEndCommandBuffer(cmd_.cb);
    // Staging buffers go once the GPU is done with them, or at once when nothing was submitted.
    auto release = [&](){ for (BufferRec& b : cmd_.staging) freeBuffer(b); cmd_.staging.clear(); };
    if (!cmd_.error.empty()){ err = cmd_.error; release(); return false; }
    if (r != VK_SUCCESS){ err = vkError("vkEndCommandBuffer", r); release(); return false; }
    if (injectedLoss_){ lost_ = true; err = vkError("vkQueueSubmit", VK_ERROR_DEVICE_LOST) + " (injected by Device::loseForTest; no driver loss)"; release(); return false; }
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd_.cb;
    vkResetFences(dev_, 1, &fence_);
    r = vkQueueSubmit(queue_, 1, &si, fence_);
    if (r == VK_SUCCESS) r = vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS){ if (r == VK_ERROR_DEVICE_LOST) lost_ = true; err = vkError("vkQueueSubmit", r); return false; }   // keep staging alive
    release();
    return validationErrors(err);
}
bool VkrDevice::loseForTest(std::string& err){
    (void)err;
    injectedLoss_ = true;   // Vulkan has no call that removes a device; the next submission reports a loss
    return true;
}
}  // namespace raw::rhi::vkr

namespace raw::rhi {
const char* linkedBackend(){ return "vulkan"; }
Device* device(std::string& err){
    struct Once { std::unique_ptr<vkr::VkrDevice> dev; std::string error; bool tried{false}; };
    static Once once;
    if (!once.tried){
        once.tried = true;
        auto d = std::make_unique<vkr::VkrDevice>();
        if (d->init(once.error)) once.dev = std::move(d);
    }
    if (!once.dev) err = once.error;
    return once.dev.get();
}
std::unique_ptr<Device> createDevice(std::string& err){
    auto d = std::make_unique<vkr::VkrDevice>();
    std::string e;
    if (!d->init(e)){ err = e; return nullptr; }
    return d;
}
}  // namespace raw::rhi
