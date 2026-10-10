// HW H1.0, Vulkan half: what a physical device states about each H1 feature, and the
// extensions and feature bits to enable for the ones it offers (vk_context.hpp).
#include "vk_context.hpp"
#include <cstdio>
#include <cstring>
#include <set>
namespace raw::rhi::vk {

struct FeatureChain {
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceOpacityMicromapFeaturesEXT omm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT serExt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT};
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV serNv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV};
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR coop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT dbuf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
};
void freeChain(FeatureChain* c){ delete c; }
void* chainHead(FeatureChain* c){ return &c->f2; }
std::string vkError(const char* what, VkResult r){ return std::string(what) + " failed: VkResult " + std::to_string((int)r); }

namespace {
// Link `s` after `tail` when `use` is true; returns the new tail.
template<class S> void* link(void* tail, S& s, bool use){
    if (!use) return tail;
    reinterpret_cast<VkBaseOutStructure*>(tail)->pNext = reinterpret_cast<VkBaseOutStructure*>(&s);
    return &s;
}
std::string ver(uint32_t v){ char b[32]; std::snprintf(b, sizeof b, "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v)); return b; }
const char* compType(VkComponentTypeKHR t){
    switch (t){ case VK_COMPONENT_TYPE_FLOAT16_KHR: return "f16"; case VK_COMPONENT_TYPE_FLOAT32_KHR: return "f32";
        case VK_COMPONENT_TYPE_SINT8_KHR: return "s8"; case VK_COMPONENT_TYPE_UINT8_KHR: return "u8";
        case VK_COMPONENT_TYPE_SINT32_KHR: return "s32"; case VK_COMPONENT_TYPE_UINT32_KHR: return "u32"; default: return "other"; }
}
}  // namespace

hw::Probe probeDevice(VkInstance inst, VkPhysicalDevice phys, std::vector<const char*>& enable, FeatureChain*& out){
    hw::Probe p;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);
    p.backend = "vulkan " + ver(props.apiVersion);
    p.adapter = props.deviceName;
    p.driver = std::to_string(props.driverVersion);
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, exts.data());
    std::set<std::string> has;
    for (auto& e : exts) has.insert(e.extensionName);
    auto ext = [&](const char* e){ return has.count(e) > 0; };
    const bool v13 = props.apiVersion >= VK_API_VERSION_1_3;
    const bool asx = ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) && ext(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    auto* q = new FeatureChain;
    void* t = &q->f2;
    t = link(t, q->v12, true);
    t = link(t, q->v13, v13);
    t = link(t, q->as, asx);
    t = link(t, q->rtp, asx && ext(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME));
    t = link(t, q->rq, asx && ext(VK_KHR_RAY_QUERY_EXTENSION_NAME));
    t = link(t, q->omm, asx && ext(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME));
    t = link(t, q->serExt, ext(VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME));
    t = link(t, q->serNv, ext(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME));
    t = link(t, q->mesh, ext(VK_EXT_MESH_SHADER_EXTENSION_NAME));
    t = link(t, q->coop, ext(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME));
    t = link(t, q->dbuf, ext(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME));
    vkGetPhysicalDeviceFeatures2(phys, &q->f2);

    VkPhysicalDeviceSubgroupProperties sg{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &sg};
    vkGetPhysicalDeviceProperties2(phys, &p2);

    const bool rtp = q->rtp.rayTracingPipeline && q->as.accelerationStructure;
    const bool rq = q->rq.rayQuery && q->as.accelerationStructure;
    p.add(hw::kRayPipeline, rtp, "VK_KHR_ray_tracing_pipeline rayTracingPipeline", rtp ? "" : "not offered");
    p.add(hw::kRayQuery, rq, "VK_KHR_ray_query rayQuery and VK_KHR_acceleration_structure", rq ? "" : "not offered");
    p.add(hw::kOpacityMicromap, q->omm.micromap == VK_TRUE, "VK_EXT_opacity_micromap micromap", "");
    const bool serE = q->serExt.rayTracingInvocationReorder == VK_TRUE, serN = q->serNv.rayTracingInvocationReorder == VK_TRUE;
    p.add(hw::kReorder, (serE || serN) && rtp, "VK_EXT_ray_tracing_invocation_reorder or VK_NV_ray_tracing_invocation_reorder",
          serE ? "EXT" : serN ? "NV only" : "not offered");
    const bool mesh = q->mesh.meshShader && q->mesh.taskShader;
    p.add(hw::kMeshShader, mesh, "VK_EXT_mesh_shader meshShader and taskShader", "");
    const bool arith = (sg.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) && (sg.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT);
    p.add(hw::kWaveOps, arith, "VkPhysicalDeviceSubgroupProperties arithmetic in compute", "subgroup size " + std::to_string(sg.subgroupSize));
    p.add(hw::kNative16, q->v12.shaderFloat16 == VK_TRUE, "VkPhysicalDeviceVulkan12Features.shaderFloat16", "");
    p.add(hw::kInt64, q->f2.features.shaderInt64 == VK_TRUE, "VkPhysicalDeviceFeatures.shaderInt64", "");
    std::string coopDetail;
    if (q->coop.cooperativeMatrix){
        auto fn = (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
        uint32_t m = 0;
        if (fn && fn(phys, &m, nullptr) == VK_SUCCESS && m){
            std::vector<VkCooperativeMatrixPropertiesKHR> cm(m, {VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR});
            fn(phys, &m, cm.data());
            for (auto& c : cm){
                char b[96];
                std::snprintf(b, sizeof b, "%s%ux%ux%u %sx%s+%s=%s", coopDetail.empty() ? "" : "; ", c.MSize, c.NSize, c.KSize,
                              compType(c.AType), compType(c.BType), compType(c.CType), compType(c.ResultType));
                coopDetail += b;
            }
        }
    }
    p.add(hw::kCoopMatrix, q->coop.cooperativeMatrix == VK_TRUE, "VK_KHR_cooperative_matrix cooperativeMatrix", coopDetail);
    const bool bindless = q->v12.descriptorIndexing && q->v12.runtimeDescriptorArray && q->v12.descriptorBindingPartiallyBound;
    p.add(hw::kBindless, bindless, "VkPhysicalDeviceVulkan12Features descriptor indexing",
          q->dbuf.descriptorBuffer ? "VK_EXT_descriptor_buffer offered" : "no descriptor buffers");
    p.add(hw::kEnhancedBarriers, v13 && q->v13.synchronization2, "VkPhysicalDeviceVulkan13Features.synchronization2", "");
    p.add(hw::kWorkGraphs, false, "no Vulkan work graphs extension in Vulkan SDK 1.4.341 core headers", "");
    uint32_t nf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nf, nullptr);
    std::vector<VkQueueFamilyProperties> fams(nf);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nf, fams.data());
    bool async = false; uint32_t bits = 0;
    for (auto& f : fams){
        if ((f.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(f.queueFlags & VK_QUEUE_GRAPHICS_BIT)) async = true;
        if (f.queueFlags & VK_QUEUE_COMPUTE_BIT) bits = f.timestampValidBits > bits ? f.timestampValidBits : bits;
    }
    p.add(hw::kAsyncCompute, async, "a queue family with compute and without graphics", std::to_string(nf) + " queue families");
    char ts[96]; std::snprintf(ts, sizeof ts, "period %.4g ns, %u valid bits", props.limits.timestampPeriod, bits);
    p.add(hw::kTimestamps, bits > 0 && props.limits.timestampComputeAndGraphics, "timestampValidBits and timestampComputeAndGraphics", ts);

    // The enable chain: only the bits H1 uses, for the features reported supported.
    auto* e = new FeatureChain;
    void* et = &e->f2;
    et = link(et, e->v12, true);
    e->v12.shaderFloat16 = p.on(hw::kNative16);
    e->v12.timelineSemaphore = q->v12.timelineSemaphore;
    e->v12.bufferDeviceAddress = q->v12.bufferDeviceAddress && (rq || rtp);
    if (p.on(hw::kBindless)){ e->v12.descriptorIndexing = e->v12.runtimeDescriptorArray = e->v12.descriptorBindingPartiallyBound = VK_TRUE; }
    e->f2.features.shaderInt64 = p.on(hw::kInt64);
    et = link(et, e->v13, v13);
    e->v13.synchronization2 = v13 && q->v13.synchronization2;
    const bool wantAs = p.on(hw::kRayQuery) || p.on(hw::kRayPipeline);
    et = link(et, e->as, wantAs); e->as.accelerationStructure = VK_TRUE;
    et = link(et, e->rq, p.on(hw::kRayQuery)); e->rq.rayQuery = VK_TRUE;
    et = link(et, e->rtp, p.on(hw::kRayPipeline)); e->rtp.rayTracingPipeline = VK_TRUE;
    et = link(et, e->omm, p.on(hw::kOpacityMicromap) && wantAs); e->omm.micromap = VK_TRUE;
    et = link(et, e->serExt, p.on(hw::kReorder) && serE && p.on(hw::kRayPipeline)); e->serExt.rayTracingInvocationReorder = VK_TRUE;
    et = link(et, e->serNv, p.on(hw::kReorder) && !serE && serN && p.on(hw::kRayPipeline)); e->serNv.rayTracingInvocationReorder = VK_TRUE;
    et = link(et, e->mesh, p.on(hw::kMeshShader)); e->mesh.meshShader = e->mesh.taskShader = VK_TRUE;
    et = link(et, e->coop, p.on(hw::kCoopMatrix)); e->coop.cooperativeMatrix = VK_TRUE;
    if (wantAs){ enable.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME); enable.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME); }
    if (p.on(hw::kRayQuery)) enable.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    if (p.on(hw::kRayPipeline)) enable.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    if (p.on(hw::kOpacityMicromap) && wantAs) enable.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
    if (p.on(hw::kReorder) && p.on(hw::kRayPipeline))
        enable.push_back(serE ? VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME : VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
    if (p.on(hw::kMeshShader)) enable.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    if (p.on(hw::kCoopMatrix)) enable.push_back(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
    delete q;
    out = e;
    return p;
}

}  // namespace raw::rhi::vk
