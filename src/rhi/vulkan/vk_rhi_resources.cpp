// The Vulkan RHI's textures, samplers and pipelines (see vk_rhi.hpp).
#include "vk_rhi.hpp"
namespace raw::rhi::vkr {
namespace {
VkDescriptorType descriptorOf(Binding b){ return b == Binding::Uniform ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; }
VkDescriptorType descriptorOf(RasterBinding b){
    switch (b){
        case RasterBinding::Uniform: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case RasterBinding::Texture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case RasterBinding::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
        default: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
}
VkCompareOp compareOf(CompareFunc c){
    switch (c){ case CompareFunc::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL; case CompareFunc::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareFunc::Always: return VK_COMPARE_OP_ALWAYS; default: return VK_COMPARE_OP_LESS; }
}
bool shader(VkDevice d, const ShaderCode& c, VkShaderModule& out, const std::string& pass, std::string& err){
    if (c.format != ShaderFormat::Spirv || !c.bytes || c.size % 4){ err = "pass " + pass + ": the Vulkan backend takes SPIR-V"; return false; }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = c.size; ci.pCode = (const uint32_t*)c.bytes;
    const VkResult r = vkCreateShaderModule(d, &ci, nullptr, &out);
    if (r != VK_SUCCESS){ err = vkError("vkCreateShaderModule " + pass, r); return false; }
    return true;
}
// Set 0, binding i of type types[i], visible to every stage the pipeline has.
bool layoutOf(VkDevice d, const std::vector<VkDescriptorType>& types, VkShaderStageFlags stages, PipeRec& p, std::string& err){
    std::vector<VkDescriptorSetLayoutBinding> b(types.size());
    for (uint32_t i = 0; i < types.size(); ++i){ b[i].binding = i; b[i].descriptorType = types[i]; b[i].descriptorCount = 1; b[i].stageFlags = stages; }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = (uint32_t)b.size(); dl.pBindings = b.data();
    VkResult r = vkCreateDescriptorSetLayout(d, &dl, nullptr, &p.dsl);
    if (r != VK_SUCCESS){ err = vkError("vkCreateDescriptorSetLayout", r); return false; }
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &p.dsl;
    if ((r = vkCreatePipelineLayout(d, &pl, nullptr, &p.layout)) != VK_SUCCESS){ err = vkError("vkCreatePipelineLayout", r); return false; }
    p.types = types;
    return true;
}
}  // namespace

VkFormat vkFormat(TextureFormat f){
    switch (f){
        case TextureFormat::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::R32Float: return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::RGB10A2Unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;   // red in the low bits, as DXGI's R10G10B10A2
        case TextureFormat::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

void VkrDevice::release(TexRec& t){
    if (t.view) vkDestroyImageView(dev_, t.view, nullptr);
    if (t.owned && t.img) vkDestroyImage(dev_, t.img, nullptr);
    if (t.owned && t.mem) vkFreeMemory(dev_, t.mem, nullptr);
    t = {};
}
void VkrDevice::release(PipeRec& p){
    if (p.pipe) vkDestroyPipeline(dev_, p.pipe, nullptr);
    if (p.layout) vkDestroyPipelineLayout(dev_, p.layout, nullptr);
    if (p.dsl) vkDestroyDescriptorSetLayout(dev_, p.dsl, nullptr);
    p = {};
}

TextureHandle VkrDevice::createTexture(const TextureDesc& desc, std::string& err){
    if (desc.width == 0 || desc.height == 0){ err = "vulkan: textures are not empty"; return {}; }
    const bool depth = isDepth(desc.format);
    TexRec t;
    t.w = desc.width; t.h = desc.height; t.format = desc.format; t.usage = desc.usage;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = vkFormat(desc.format); ii.extent = {desc.width, desc.height, 1};
    ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (has(desc.usage, TextureUsage::Sampled)) ii.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (has(desc.usage, TextureUsage::RenderTarget)) ii.usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(dev_, &ii, nullptr, &t.img);
    if (r != VK_SUCCESS){ err = vkError("vkCreateImage", r); return {}; }
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(dev_, t.img, &mr);
    uint32_t type = 0;
    if (!memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type) && !memoryType(mr.memoryTypeBits, 0, type)){
        err = "vulkan: no memory type for a texture"; release(t); return {}; }
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size; ma.memoryTypeIndex = type;
    if ((r = vkAllocateMemory(dev_, &ma, nullptr, &t.mem)) != VK_SUCCESS){ err = vkError("vkAllocateMemory (texture)", r); release(t); return {}; }
    vkBindImageMemory(dev_, t.img, t.mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format;
    vi.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
    if ((r = vkCreateImageView(dev_, &vi, nullptr, &t.view)) != VK_SUCCESS){ err = vkError("vkCreateImageView", r); release(t); return {}; }
    auto id = textures_.insert(t);
    return {id.index, id.gen};
}
void VkrDevice::destroyTexture(TextureHandle h){
    TexRec t;
    if (textures_.erase(h.index, h.gen, t)) release(t);
}

SamplerHandle VkrDevice::createSampler(const SamplerDesc& desc, std::string& err){
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = desc.filter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    si.mipmapMode = desc.filter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW =
        desc.address == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE;
    SampRec s;
    const VkResult r = vkCreateSampler(dev_, &si, nullptr, &s.s);
    if (r != VK_SUCCESS){ err = vkError("vkCreateSampler", r); return {}; }
    auto id = samplers_.insert(s);
    return {id.index, id.gen};
}

PipelineHandle VkrDevice::createComputePipeline(const ComputePipelineDesc& desc, std::string& err){
    PipeRec p;
    std::vector<VkDescriptorType> types;
    for (Binding b : desc.layout) types.push_back(descriptorOf(b));
    VkShaderModule m = VK_NULL_HANDLE;
    if (!shader(dev_, desc.code, m, desc.label, err)) return {};
    if (!layoutOf(dev_, types, VK_SHADER_STAGE_COMPUTE_BIT, p, err)){ vkDestroyShaderModule(dev_, m, nullptr); release(p); return {}; }
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", nullptr};
    ci.layout = p.layout;
    const VkResult r = vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &ci, nullptr, &p.pipe);
    vkDestroyShaderModule(dev_, m, nullptr);
    if (r != VK_SUCCESS){ err = vkError(std::string("vkCreateComputePipelines ") + desc.label, r); release(p); return {}; }
    auto id = pipes_.insert(p);
    return {id.index, id.gen};
}

// Both stages enter at "main": the build compiles each HLSL entry with glslang's --sep <entry> -e main.
RasterPipelineHandle VkrDevice::createRasterPipeline(const RasterPipelineDesc& desc, std::string& err){
    const std::string pass = desc.label;
    if (desc.targetCount == 0 || desc.targetCount > kMaxColorTargets){ err = "raster pass " + pass + ": 1 to 4 colour targets"; return {}; }
    PipeRec p;
    std::vector<VkDescriptorType> types;
    for (RasterBinding b : desc.layout) types.push_back(descriptorOf(b));
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    if (!shader(dev_, desc.vertex, vs, pass, err)) return {};
    if (!shader(dev_, desc.fragment, fs, pass, err)){ vkDestroyShaderModule(dev_, vs, nullptr); return {}; }
    auto cleanup = [&]{ vkDestroyShaderModule(dev_, vs, nullptr); vkDestroyShaderModule(dev_, fs, nullptr); };
    if (!layoutOf(dev_, types, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, p, err)){ cleanup(); release(p); return {}; }
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = desc.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT : desc.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;   // with the flipped viewport, as WebGPU's default
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = desc.depth.enabled; ds.depthWriteEnable = desc.depth.enabled && desc.depth.write;
    ds.depthCompareOp = compareOf(desc.depth.compare);
    VkPipelineColorBlendAttachmentState blend[kMaxColorTargets]{};
    VkFormat formats[kMaxColorTargets]{};
    for (uint32_t k = 0; k < desc.targetCount; ++k){
        if (isDepth(desc.targets[k])){ cleanup(); release(p); err = "raster pass " + pass + ": a colour target cannot be a depth format"; return {}; }
        blend[k].colorWriteMask = 0xF; formats[k] = vkFormat(desc.targets[k]);
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = desc.targetCount; cb.pAttachments = blend;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = desc.targetCount; ri.pColorAttachmentFormats = formats;
    ri.depthAttachmentFormat = desc.depth.enabled ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.pNext = &ri; gi.stageCount = 2; gi.pStages = stages;
    gi.pVertexInputState = &vin; gi.pInputAssemblyState = &ia; gi.pViewportState = &vp; gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms; gi.pDepthStencilState = &ds; gi.pColorBlendState = &cb; gi.pDynamicState = &dy;
    gi.layout = p.layout;
    const VkResult r = vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gi, nullptr, &p.pipe);
    cleanup();
    if (r != VK_SUCCESS){ err = vkError("vkCreateGraphicsPipelines " + pass, r); release(p); return {}; }
    p.targetCount = desc.targetCount; p.depth = desc.depth.enabled;
    auto id = rasters_.insert(p);
    return {id.index, id.gen};
}
}  // namespace raw::rhi::vkr
