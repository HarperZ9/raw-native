// The WebGPU backend's textures, samplers, raster pipelines and render passes (RHI
// versions 2 and 3: formats, several colour targets, depth, culling, storage in raster stages). WebGPU tracks texture usage itself, so nothing
// here records a transition. A raster pipeline uses the layout WebGPU derives from the
// WGSL, and the RHI's layout is checked against each draw.
#include "webgpu_device.hpp"
namespace raw::rhi::webgpu {

TextureHandle WebGpuDevice::createTexture(const TextureDesc& d, std::string& err){
    if (d.width == 0 || d.height == 0){ err = "webgpu: textures are not empty"; return {}; }
    WGPUTextureUsage u = WGPUTextureUsage_None;
    if (has(d.usage, TextureUsage::Sampled)) u |= WGPUTextureUsage_TextureBinding;
    if (has(d.usage, TextureUsage::RenderTarget)) u |= WGPUTextureUsage_RenderAttachment;
    if (has(d.usage, TextureUsage::CopyDst)) u |= WGPUTextureUsage_CopyDst;
    if (has(d.usage, TextureUsage::CopySrc)) u |= WGPUTextureUsage_CopySrc;
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.label = sv(d.label); td.usage = u; td.dimension = WGPUTextureDimension_2D;
    td.size = {d.width, d.height, 1}; td.format = wgpuFormat(d.format); td.mipLevelCount = 1; td.sampleCount = 1;
    wgpuDevicePushErrorScope(dev, WGPUErrorFilter_Validation);
    TexRec t;
    t.tex = wgpuDeviceCreateTexture(dev, &td);
    t.view = t.tex ? wgpuTextureCreateView(t.tex, nullptr) : nullptr;
    t.w = d.width; t.h = d.height; t.format = d.format;
    if (!popScope("texture creation", err) || !t.view) return {};
    auto id = textures.insert(t);
    return {id.index, id.gen};
}
void WebGpuDevice::destroyTexture(TextureHandle h){
    if (auto t = textures.erase(h.index, h.gen)){ wgpuTextureViewRelease(t->view); wgpuTextureDestroy(t->tex); wgpuTextureRelease(t->tex); }
}
SamplerHandle WebGpuDevice::createSampler(const SamplerDesc& d, std::string& err){
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sd.label = sv(d.label);
    const WGPUAddressMode am = d.address == AddressMode::Repeat ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    sd.addressModeU = sd.addressModeV = sd.addressModeW = am;
    const WGPUFilterMode fm = d.filter == Filter::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    sd.magFilter = sd.minFilter = fm;
    sd.mipmapFilter = d.filter == Filter::Nearest ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
    sd.lodMinClamp = 0.0f; sd.lodMaxClamp = 32.0f; sd.maxAnisotropy = 1;
    WGPUSampler s = wgpuDeviceCreateSampler(dev, &sd);
    if (!s){ err = "webgpu: sampler creation failed"; return {}; }
    auto id = samplers.insert(s);
    return {id.index, id.gen};
}
WGPUShaderModule WebGpuDevice::module(const ShaderCode& code){
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = WGPUStringView{(const char*)code.bytes, code.size};
    WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    md.nextInChain = &wgsl.chain;
    return wgpuDeviceCreateShaderModule(dev, &md);
}
RasterPipelineHandle WebGpuDevice::createRasterPipeline(const RasterPipelineDesc& d, std::string& err){
    const std::string what = std::string("raster pass ") + d.label;
    if (d.vertex.format != ShaderFormat::Wgsl || d.fragment.format != ShaderFormat::Wgsl || !d.vertex.bytes || !d.fragment.bytes){
        err = what + ": the WebGPU backend takes WGSL"; return {}; }
    wgpuDevicePushErrorScope(dev, WGPUErrorFilter_Validation);
    WGPUShaderModule vm = module(d.vertex), fm = module(d.fragment);
    if (d.targetCount == 0 || d.targetCount > kMaxColorTargets){ wgpuShaderModuleRelease(vm); wgpuShaderModuleRelease(fm); popScope(what.c_str(), err);
        err = what + ": 1 to 4 colour targets"; return {}; }
    WGPUColorTargetState ct[kMaxColorTargets];
    for (uint32_t k = 0; k < d.targetCount; ++k){ ct[k] = WGPU_COLOR_TARGET_STATE_INIT; ct[k].format = wgpuFormat(d.targets[k]); ct[k].writeMask = WGPUColorWriteMask_All; }
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = fm; fs.entryPoint = sv("fs"); fs.targetCount = d.targetCount; fs.targets = ct;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (d.depth.enabled){
        ds.format = WGPUTextureFormat_Depth32Float;
        ds.depthWriteEnabled = d.depth.write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare = d.depth.compare == CompareFunc::LessEqual ? WGPUCompareFunction_LessEqual : d.depth.compare == CompareFunc::Equal ? WGPUCompareFunction_Equal
                        : d.depth.compare == CompareFunc::Always ? WGPUCompareFunction_Always : WGPUCompareFunction_Less;
    }
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.label = sv(d.label);
    pd.vertex.module = vm; pd.vertex.entryPoint = sv("vs");
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList; pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = d.cull == CullMode::Back ? WGPUCullMode_Back : d.cull == CullMode::Front ? WGPUCullMode_Front : WGPUCullMode_None;
    if (d.depth.enabled) pd.depthStencil = &ds;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(dev, &pd);
    wgpuShaderModuleRelease(vm); wgpuShaderModuleRelease(fm);
    if (!popScope(what.c_str(), err) || !p) return {};
    auto id = rasters.insert(RasterRec{p, std::vector<RasterBinding>(d.layout.begin(), d.layout.end()), d.targetCount, d.depth.enabled});
    return {id.index, id.gen};
}

void WebGpuCommandList::uploadTexture(TextureHandle dst, const void* data, uint32_t width, uint32_t height){
    TexRec* t = dev.textures.get(dst.index, dst.gen);
    if (!t){ fail("webgpu: upload to an unknown texture"); return; }
    if (width != t->w || height != t->h){ fail("webgpu: texture upload size differs from the texture"); return; }
    WGPUTexelCopyTextureInfo to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    to.texture = t->tex;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    if (isDepth(t->format)){ fail("webgpu: depth textures take no uploads"); return; }
    const uint32_t bpt = bytesPerTexel(t->format);
    layout.bytesPerRow = width * bpt; layout.rowsPerImage = height;
    const WGPUExtent3D size{width, height, 1};
    wgpuQueueWriteTexture(dev.queue, &to, data, size_t(width) * height * bpt, &layout, &size);   // ordered before this list's submit
}
void WebGpuCommandList::copyTextureToBuffer(TextureHandle src, BufferHandle dst){
    TexRec* t = dev.textures.get(src.index, src.gen); WGPUBuffer* b = dev.buffer(dst);
    if (!t || !b){ fail("webgpu: texture copy with an unknown texture or buffer"); return; }
    WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    from.texture = t->tex;
    if (isDepth(t->format)) from.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTexelCopyBufferInfo to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    to.buffer = *b; to.layout.offset = 0; to.layout.bytesPerRow = textureRowPitch(t->w, t->format); to.layout.rowsPerImage = t->h;
    const WGPUExtent3D size{t->w, t->h, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &from, &to, &size);
}
void WebGpuCommandList::beginRenderPass(const RenderPassDesc& p){
    if (p.colorCount == 0 || p.colorCount > kMaxColorTargets){ fail("webgpu: a render pass has 1 to 4 colour targets"); return; }
    WGPURenderPassColorAttachment ca[kMaxColorTargets];
    for (uint32_t k = 0; k < p.colorCount; ++k){
        TexRec* t = dev.textures.get(p.color[k].texture.index, p.color[k].texture.gen);
        if (!t){ fail("webgpu: render pass on an unknown texture"); return; }
        ca[k] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        ca[k].view = t->view; ca[k].loadOp = WGPULoadOp_Clear; ca[k].storeOp = WGPUStoreOp_Store;
        ca[k].clearValue = {p.color[k].clear[0], p.color[k].clear[1], p.color[k].clear[2], p.color[k].clear[3]};
    }
    WGPURenderPassDescriptor rd = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rd.colorAttachmentCount = p.colorCount; rd.colorAttachments = ca;
    WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    if (p.depth.valid()){
        TexRec* d = dev.textures.get(p.depth.index, p.depth.gen);
        if (!d){ fail("webgpu: render pass on an unknown depth texture"); return; }
        da.view = d->view; da.depthLoadOp = WGPULoadOp_Clear; da.depthStoreOp = WGPUStoreOp_Store; da.depthClearValue = p.depthClear;
        rd.depthStencilAttachment = &da;
    }
    passTargets = p.colorCount; passDepth = p.depth.valid();
    pass = wgpuCommandEncoderBeginRenderPass(enc, &rd);
}
void WebGpuCommandList::draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount){
    RasterRec* r = dev.rasters.get(pipeline.index, pipeline.gen);
    if (!r){ fail("webgpu: draw with an unknown raster pipeline"); return; }
    if (!pass){ fail("webgpu: draw outside a render pass"); return; }
    if (binds.size() != r->layout.size()){ fail("webgpu: draw binding count differs from the pipeline layout"); return; }
    if (r->targetCount != passTargets || r->depth != passDepth){ fail("webgpu: the pipeline's targets differ from the render pass's"); return; }
    std::vector<WGPUBindGroupEntry> e(binds.size());
    for (size_t i = 0; i < e.size(); ++i){
        e[i] = WGPU_BIND_GROUP_ENTRY_INIT; e[i].binding = (uint32_t)i;
        if (r->layout[i] == RasterBinding::Uniform || r->layout[i] == RasterBinding::StorageRead){
            WGPUBuffer* b = dev.buffer(binds[i].buffer);
            if (!b){ fail("webgpu: draw binds an unknown buffer"); return; }
            e[i].buffer = *b; e[i].size = WGPU_WHOLE_SIZE;
        } else if (r->layout[i] == RasterBinding::Texture){
            TexRec* t = dev.textures.get(binds[i].texture.index, binds[i].texture.gen);
            if (!t){ fail("webgpu: draw binds an unknown texture"); return; }
            e[i].textureView = t->view;
        } else {
            WGPUSampler* s = dev.samplers.get(binds[i].sampler.index, binds[i].sampler.gen);
            if (!s){ fail("webgpu: draw binds an unknown sampler"); return; }
            e[i].sampler = *s;
        }
    }
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(r->pipe, 0);
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bd.layout = layout; bd.entryCount = e.size(); bd.entries = e.data();
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(dev.dev, &bd);
    wgpuBindGroupLayoutRelease(layout);
    groups.push_back(g);
    wgpuRenderPassEncoderSetPipeline(pass, r->pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, vertexCount, 1, 0, 0);
}
void WebGpuCommandList::endRenderPass(){
    if (!pass){ fail("webgpu: endRenderPass without a render pass"); return; }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    pass = nullptr; passTargets = 0; passDepth = false;
}
}  // namespace raw::rhi::webgpu
