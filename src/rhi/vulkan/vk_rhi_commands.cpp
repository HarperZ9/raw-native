// The Vulkan RHI command list: barriers, copies, dispatches and render passes recorded into one
// command buffer (see vk_rhi.hpp). A render pass begins rendering lazily, at its first draw or at
// its end, so textures a draw samples can be moved to SHADER_READ_ONLY outside the rendering.
#include "vk_rhi.hpp"
#include <cstring>
namespace raw::rhi::vkr {
namespace {
constexpr VkPipelineStageFlags kShaders = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
void bufferUse(Access a, VkPipelineStageFlags& stage, VkAccessFlags& access){
    switch (a){
        case Access::Uniform: stage = kShaders; access = VK_ACCESS_UNIFORM_READ_BIT; return;
        case Access::StorageRead: stage = kShaders; access = VK_ACCESS_SHADER_READ_BIT; return;
        case Access::StorageWrite: stage = kShaders; access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; return;
        case Access::CopySrc: stage = VK_PIPELINE_STAGE_TRANSFER_BIT; access = VK_ACCESS_TRANSFER_READ_BIT; return;
        case Access::CopyDst: stage = VK_PIPELINE_STAGE_TRANSFER_BIT; access = VK_ACCESS_TRANSFER_WRITE_BIT; return;
        case Access::Undefined: break;
    }
    stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; access = 0;
}
void layoutUse(VkImageLayout l, bool asSource, VkPipelineStageFlags& stage, VkAccessFlags& access){
    switch (l){
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; return;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; return;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: stage = kShaders; access = VK_ACCESS_SHADER_READ_BIT; return;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: stage = VK_PIPELINE_STAGE_TRANSFER_BIT; access = VK_ACCESS_TRANSFER_READ_BIT; return;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: stage = VK_PIPELINE_STAGE_TRANSFER_BIT; access = VK_ACCESS_TRANSFER_WRITE_BIT; return;
        default: break;   // UNDEFINED, and PRESENT_SRC (acquired behind a fence the host waited on)
    }
    stage = asSource ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; access = 0;
}
VkImageAspectFlags aspectOf(TextureFormat f){ return isDepth(f) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT; }
}  // namespace

void VkrCommandList::transition(TexRec& t, VkImageLayout to){
    if (t.layout == to) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkPipelineStageFlags src, dst;
    layoutUse(t.layout, true, src, b.srcAccessMask);
    layoutUse(to, false, dst, b.dstAccessMask);
    b.oldLayout = t.layout; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.img; b.subresourceRange = {aspectOf(t.format), 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    t.layout = to;
}

void VkrCommandList::barrier(std::span<const BufferBarrier> barriers){
    if (rendering){ fail("vulkan: a buffer barrier inside a render pass"); return; }
    for (const BufferBarrier& x : barriers){
        BufferRec* r = dev_.buffer(x.buffer);
        if (!r){ fail("vulkan: barrier on an unknown buffer"); return; }
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        VkPipelineStageFlags src, dst;
        bufferUse(x.before, src, b.srcAccessMask);
        bufferUse(x.after, dst, b.dstAccessMask);
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = r->buf; b.offset = 0; b.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 1, &b, 0, nullptr);
    }
}
void VkrCommandList::upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size){
    BufferRec* r = dev_.buffer(dst);
    if (!r){ fail("vulkan: upload to an unknown buffer"); return; }
    if (offset + size > r->size){ fail("vulkan: upload past the end of a buffer"); return; }
    if (r->host){ std::memcpy((unsigned char*)r->map + offset, data, (size_t)size); return; }
    if (rendering){ fail("vulkan: a staged upload inside a render pass"); return; }
    BufferRec s; std::string err;
    if (!dev_.makeBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, s, err)){ fail(err); return; }
    std::memcpy(s.map, data, (size_t)size);
    staging.push_back(s);
    const VkBufferCopy c{0, offset, size};
    vkCmdCopyBuffer(cb, s.buf, r->buf, 1, &c);
}
void VkrCommandList::copyBuffer(BufferHandle src, uint64_t so, BufferHandle dst, uint64_t dofs, uint64_t size){
    BufferRec *s = dev_.buffer(src), *d = dev_.buffer(dst);
    if (!s || !d){ fail("vulkan: copy with an unknown buffer"); return; }
    if (rendering){ fail("vulkan: a copy inside a render pass"); return; }
    const VkBufferCopy c{so, dofs, size};
    vkCmdCopyBuffer(cb, s->buf, d->buf, 1, &c);
}

VkDescriptorSet VkrCommandList::descriptors(const PipeRec& p, std::span<const RasterBind> rb, std::span<const BufferHandle> bb){
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dev_.descriptorPool(); ai.descriptorSetCount = 1; ai.pSetLayouts = &p.dsl;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (p.types.empty()) return VK_NULL_HANDLE;
    const VkResult r = vkAllocateDescriptorSets(dev_.vk(), &ai, &set);
    if (r != VK_SUCCESS){ fail(vkError("vkAllocateDescriptorSets (more than 4,096 draws in one submission?)", r)); return VK_NULL_HANDLE; }
    const size_t n = p.types.size();
    std::vector<VkDescriptorBufferInfo> bufs(n);
    std::vector<VkDescriptorImageInfo> imgs(n);
    std::vector<VkWriteDescriptorSet> w(n);
    for (size_t i = 0; i < n; ++i){
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstSet = set; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1; w[i].descriptorType = p.types[i];
        const VkDescriptorType t = p.types[i];
        if (t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){
            BufferRec* b = dev_.buffer(bb.empty() ? rb[i].buffer : bb[i]);
            if (!b){ fail("vulkan: a binding names an unknown buffer"); return VK_NULL_HANDLE; }
            bufs[i] = {b->buf, 0, VK_WHOLE_SIZE};
            w[i].pBufferInfo = &bufs[i];
        } else if (t == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE){
            TexRec* x = dev_.texture(rb[i].texture);
            if (!x){ fail("vulkan: a draw binds an unknown texture"); return VK_NULL_HANDLE; }
            imgs[i] = {VK_NULL_HANDLE, x->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            w[i].pImageInfo = &imgs[i];
        } else {
            SampRec* s = dev_.sampler(rb[i].sampler);
            if (!s){ fail("vulkan: a draw binds an unknown sampler"); return VK_NULL_HANDLE; }
            imgs[i] = {s->s, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            w[i].pImageInfo = &imgs[i];
        }
    }
    vkUpdateDescriptorSets(dev_.vk(), (uint32_t)n, w.data(), 0, nullptr);
    return set;
}

void VkrCommandList::dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z){
    PipeRec* p = dev_.pipeline(pipeline);
    if (!p){ fail("vulkan: dispatch with an unknown pipeline"); return; }
    if (rendering){ fail("vulkan: a dispatch inside a render pass"); return; }
    if (binds.size() != p->types.size()){ fail("vulkan: dispatch binding count differs from the pipeline layout"); return; }
    if (x > kMaxDispatchGroups || y > kMaxDispatchGroups || z > kMaxDispatchGroups){ fail(dispatchLimitError("vulkan", x, y, z)); return; }
    const VkDescriptorSet set = descriptors(*p, {}, binds);
    if (!error.empty()) return;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
    if (set) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(cb, x, y, z);
}

void VkrCommandList::uploadTexture(TextureHandle dst, const void* data, uint32_t width, uint32_t height){
    TexRec* t = dev_.texture(dst);
    if (!t){ fail("vulkan: upload to an unknown texture"); return; }
    if (width != t->w || height != t->h){ fail("vulkan: texture upload size differs from the texture"); return; }
    if (isDepth(t->format)){ fail("vulkan: depth textures take no uploads"); return; }
    if (rendering){ fail("vulkan: a texture upload inside a render pass"); return; }
    const uint64_t bytes = uint64_t(width) * height * bytesPerTexel(t->format);
    BufferRec s; std::string err;
    if (!dev_.makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, s, err)){ fail(err); return; }
    std::memcpy(s.map, data, (size_t)bytes);
    staging.push_back(s);
    transition(*t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cb, s.buf, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
}
void VkrCommandList::copyTextureToBuffer(TextureHandle src, BufferHandle dst){
    TexRec* t = dev_.texture(src); BufferRec* b = dev_.buffer(dst);
    if (!t || !b){ fail("vulkan: texture copy with an unknown texture or buffer"); return; }
    if (rendering){ fail("vulkan: a texture copy inside a render pass"); return; }
    const uint32_t pitch = textureRowPitch(t->w, t->format);
    if (b->size < uint64_t(pitch) * t->h){ fail("vulkan: texture copy into a buffer that is too small"); return; }
    transition(*t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.bufferRowLength = pitch / bytesPerTexel(t->format);
    c.imageSubresource = {aspectOf(t->format), 0, 0, 1}; c.imageExtent = {t->w, t->h, 1};
    vkCmdCopyImageToBuffer(cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b->buf, 1, &c);
}

void VkrCommandList::beginRenderPass(const RenderPassDesc& p){
    if (open){ fail("vulkan: a render pass is already open"); return; }
    if (p.colorCount == 0 || p.colorCount > kMaxColorTargets){ fail("vulkan: a render pass has 1 to 4 colour targets"); return; }
    uint32_t w = 0, h = 0;
    for (uint32_t k = 0; k < p.colorCount; ++k){
        TexRec* t = dev_.texture(p.color[k].texture);
        if (!t || !has(t->usage, TextureUsage::RenderTarget) || isDepth(t->format)){ fail("vulkan: render pass on a texture that is not a colour target"); return; }
        if (k && (t->w != w || t->h != h)){ fail("vulkan: render pass targets differ in size"); return; }
        w = t->w; h = t->h;
    }
    if (p.depth.valid()){
        TexRec* d = dev_.texture(p.depth);
        if (!d || !isDepth(d->format)){ fail("vulkan: render pass depth target is not a depth target"); return; }
        if (d->w != w || d->h != h){ fail("vulkan: render pass depth target differs in size"); return; }
    }
    pass = p; open = true; rendering = false; cleared_ = false;
}
void VkrCommandList::startRendering(bool load){
    VkRenderingAttachmentInfo col[kMaxColorTargets]{};
    TexRec* first = nullptr;
    for (uint32_t k = 0; k < pass.colorCount; ++k){
        TexRec* t = dev_.texture(pass.color[k].texture);
        if (!t){ fail("vulkan: a render target went away"); return; }
        if (!first) first = t;
        transition(*t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        col[k] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        col[k].imageView = t->view; col[k].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        col[k].loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR; col[k].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(col[k].clearValue.color.float32, pass.color[k].clear, sizeof(float) * 4);
    }
    VkRenderingAttachmentInfo dep{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (pass.depth.valid()){
        TexRec* d = dev_.texture(pass.depth);
        transition(*d, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        dep.imageView = d->view; dep.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        dep.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR; dep.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        dep.clearValue.depthStencil = {pass.depthClear, 0};
    }
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {first->w, first->h}}; ri.layerCount = 1;
    ri.colorAttachmentCount = pass.colorCount; ri.pColorAttachments = col;
    ri.pDepthAttachment = pass.depth.valid() ? &dep : nullptr;
    vkCmdBeginRendering(cb, &ri);
    // y-up clip space, as D3D12 and WebGPU: a viewport of negative height from the bottom row.
    const VkViewport vp{0, float(first->h), float(first->w), -float(first->h), 0, 1};
    const VkRect2D sc{{0, 0}, {first->w, first->h}};
    vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc);
    rendering = true; cleared_ = true;
}
void VkrCommandList::draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount){
    PipeRec* p = dev_.raster(pipeline);
    if (!p){ fail("vulkan: draw with an unknown raster pipeline"); return; }
    if (!open){ fail("vulkan: draw outside a render pass"); return; }
    if (p->targetCount != pass.colorCount || p->depth != pass.depth.valid()){ fail("vulkan: the pipeline's targets differ from the render pass's"); return; }
    if (binds.size() != p->types.size()){ fail("vulkan: draw binding count differs from the pipeline layout"); return; }
    bool moves = false;
    for (size_t i = 0; i < binds.size(); ++i){
        if (p->types[i] != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) continue;
        TexRec* t = dev_.texture(binds[i].texture);
        if (!t || !has(t->usage, TextureUsage::Sampled)){ fail("vulkan: draw binds a texture that is not sampled"); return; }
        for (uint32_t k = 0; k < pass.colorCount; ++k) if (binds[i].texture == pass.color[k].texture){ fail("vulkan: a draw samples its own render target"); return; }
        if (binds[i].texture == pass.depth){ fail("vulkan: a draw samples its own depth target"); return; }
        moves = moves || t->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (moves && rendering){ vkCmdEndRendering(cb); rendering = false; }
    for (size_t i = 0; moves && i < binds.size(); ++i)
        if (p->types[i] == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) transition(*dev_.texture(binds[i].texture), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!rendering) startRendering(cleared_);
    const VkDescriptorSet set = descriptors(*p, binds, {});
    if (!error.empty()) return;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipe);
    if (set) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cb, vertexCount, 1, 0, 0);
}
void VkrCommandList::endRenderPass(){
    if (!open) return;
    if (!cleared_ && error.empty()) startRendering(false);   // a pass with no draws still clears
    if (rendering) vkCmdEndRendering(cb);
    rendering = false; open = false; pass = {};
}
}  // namespace raw::rhi::vkr
