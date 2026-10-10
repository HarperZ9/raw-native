// The HW workstream's Vulkan context (vk_context.hpp).
#include "vk_context.hpp"
#include <cstdlib>
#include <cstring>
namespace raw::rhi::vk {
namespace {
bool envOn(const char* n){ const char* v = std::getenv(n); return v && std::strcmp(v, "1") == 0; }
}

// Errors of type VALIDATION are findings about this program's API use and fail a run; loader
// messages (type GENERAL, e.g. another application's broken implicit layer manifest) are
// counted apart (evidence/hw-h1-0-vk-bounds.json, method note 1).
VKAPI_ATTR VkBool32 VKAPI_CALL Context::onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT type,
                                                  const VkDebugUtilsMessengerCallbackDataEXT* data, void* user){
    auto* self = static_cast<Context*>(user);
    if (!(sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) return VK_FALSE;
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT){
        if (self->validationErrors_++ == 0 && data && data->pMessage) self->firstError_ = data->pMessage;
    } else if (self->loaderErrors_++ == 0 && data && data->pMessage) self->firstLoaderError_ = data->pMessage;
    return VK_FALSE;
}

bool Context::createInstance(std::string& err){
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "raw-native hw"; app.pEngineName = "raw-native"; app.apiVersion = VK_API_VERSION_1_3;
    const bool validate = envOn("RAW_NATIVE_VK_VALIDATION");
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    mi.pfnUserCallback = &Context::onMessage; mi.pUserData = this;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    if (validate){ ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = &layer; ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = &ext; ci.pNext = &mi; }
    VkResult r = vkCreateInstance(&ci, nullptr, &inst_);
    if (r != VK_SUCCESS){ err = vkError(validate ? "vkCreateInstance with the validation layer" : "vkCreateInstance", r); return false; }
    if (validate){
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst_, "vkCreateDebugUtilsMessengerEXT");
        if (!fn || fn(inst_, &mi, nullptr, &messenger_) != VK_SUCCESS){ err = "could not create the debug messenger"; return false; }
    }
    return true;
}

bool Context::pickDevice(std::string& err){
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst_, &n, devs.data());
    const char* want = std::getenv("RAW_NATIVE_VK_DEVICE");
    const bool hwOk = envOn("RAW_NATIVE_VK_HW");
    std::string seen;
    for (VkPhysicalDevice d : devs){
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        seen += (seen.empty() ? "" : ", ") + std::string(p.deviceName);
        if (want && !std::strstr(p.deviceName, want)) continue;
        if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && !hwOk) continue;
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        phys_ = d;
        return true;
    }
    err = "no usable Vulkan 1.3 device (a non-CPU device needs RAW_NATIVE_VK_HW=1); seen: " + (seen.empty() ? std::string("none") : seen);
    return false;
}

bool Context::createDevice(std::string& err){
    std::vector<const char*> exts;
    FeatureChain* chain = nullptr;
    probe_ = probeDevice(inst_, phys_, exts, chain);
    uint32_t nf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &nf, nullptr);
    std::vector<VkQueueFamilyProperties> fams(nf);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &nf, fams.data());
    bool found = false;
    for (uint32_t i = 0; i < nf && !found; ++i)
        if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT){ family_ = i; tsBits_ = fams[i].timestampValidBits; found = true; }
    if (!found){ freeChain(chain); err = "no compute queue family"; return false; }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_, &props);
    tsPeriod_ = props.limits.timestampPeriod;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext = chainHead(chain);
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = (uint32_t)exts.size(); di.ppEnabledExtensionNames = exts.data();
    VkResult r = vkCreateDevice(phys_, &di, nullptr, &dev_);
    freeChain(chain);
    if (r != VK_SUCCESS){ err = vkError("vkCreateDevice", r); return false; }
    vkGetDeviceQueue(dev_, family_, 0, &queue_);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = family_;
    if ((r = vkCreateCommandPool(dev_, &pi, nullptr, &pool_)) != VK_SUCCESS){ err = vkError("vkCreateCommandPool", r); return false; }
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    if ((r = vkAllocateCommandBuffers(dev_, &ai, &cmd_)) != VK_SUCCESS){ err = vkError("vkAllocateCommandBuffers", r); return false; }
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if ((r = vkCreateFence(dev_, &fi, nullptr, &fence_)) != VK_SUCCESS){ err = vkError("vkCreateFence", r); return false; }
    if (tsBits_){
        VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qp.queryType = VK_QUERY_TYPE_TIMESTAMP; qp.queryCount = 2;
        if ((r = vkCreateQueryPool(dev_, &qp, nullptr, &queries_)) != VK_SUCCESS){ err = vkError("vkCreateQueryPool", r); return false; }
    }
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 8; dp.poolSizeCount = 1; dp.pPoolSizes = &ps;
    if ((r = vkCreateDescriptorPool(dev_, &dp, nullptr, &dpool_)) != VK_SUCCESS){ err = vkError("vkCreateDescriptorPool", r); return false; }
    return true;
}

bool Context::init(std::string& err){ return createInstance(err) && pickDevice(err) && createDevice(err); }

Context::~Context(){
    if (dev_){
        vkDeviceWaitIdle(dev_);
        if (dpool_) vkDestroyDescriptorPool(dev_, dpool_, nullptr);
        if (queries_) vkDestroyQueryPool(dev_, queries_, nullptr);
        if (fence_) vkDestroyFence(dev_, fence_, nullptr);
        if (pool_) vkDestroyCommandPool(dev_, pool_, nullptr);
        vkDestroyDevice(dev_, nullptr);
    }
    if (messenger_){
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst_, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(inst_, messenger_, nullptr);
    }
    if (inst_) vkDestroyInstance(inst_, nullptr);
}

}  // namespace raw::rhi::vk
