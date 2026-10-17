// The validating command list (raw/rhi/validate.hpp): rules V01 to V12 on recorded commands.
// A command that breaks a rule is reported and dropped; the others reach the backend's list.
#include "validate_internal.hpp"
namespace raw::rhi::validate {
namespace {
std::string n(uint64_t v){ return std::to_string(v); }
}

bool ValidatingList::open(const char* what){
    if (dev_.listOpen && inner) return true;
    dev_.report("V08", std::string(what) + " with no open list");
    return false;
}
bool ValidatingList::use(BufferHandle b, Access need, BufferUsage usage, const char* what){
    BufState* s = dev_.buffer(b);
    if (!s){ dev_.report("V01", std::string(what) + ": a handle that is not a live buffer"); return false; }
    if (!has(s->desc.usage, usage)){
        dev_.report("V03", std::string(what) + ": " + bufferName(s) + " lacks the usage the call needs"); return false; }
    if (s->state != Access::Undefined && s->state != need){
        dev_.report("V07", std::string(what) + ": " + bufferName(s) + " is used as " + accessText(need) + " while in "
                           + accessText(s->state) + ", with no barrier between");
        return false;
    }
    if (s->dirty && !(need == Access::CopyDst && s->lastWasCopy)){
        dev_.report("V07", std::string(what) + ": " + bufferName(s) + " is used after a write with no barrier between");
        return false;
    }
    return true;
}
void ValidatingList::mark(BufferHandle b, Access a){
    BufState* s = dev_.buffer(b);
    if (!s) return;
    s->state = a;
    if (isWrite(a)){ s->dirty = true; s->lastWasCopy = a == Access::CopyDst; s->writtenInList = true; }
}

void ValidatingList::barrier(std::span<const BufferBarrier> barriers){
    if (!open("barrier")) return;
    std::vector<BufferBarrier> pass;
    for (const BufferBarrier& x : barriers){
        BufState* s = dev_.buffer(x.buffer);
        if (!s){ dev_.report("V01", "barrier on a handle that is not a live buffer"); continue; }
        if (s->state != Access::Undefined && x.before != s->state){
            dev_.report("V06", "barrier on " + bufferName(s) + " from " + accessText(x.before) + ", but it is in " + accessText(s->state));
            continue;
        }
        s->state = x.after; s->dirty = false;
        pass.push_back(x);
    }
    if (!pass.empty()) inner->barrier(pass);
}
void ValidatingList::upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size){
    if (!open("upload") || !use(dst, Access::CopyDst, BufferUsage::CopyDst, "upload")) return;
    const BufState* s = dev_.buffer(dst);
    if (offset + size > s->desc.size || offset + size < offset){
        dev_.report("V02", "upload of " + n(size) + " bytes at " + n(offset) + " into " + bufferName(s) + " of " + n(s->desc.size)); return; }
    inner->upload(dst, offset, data, size);
    mark(dst, Access::CopyDst);
}
void ValidatingList::copyBuffer(BufferHandle src, uint64_t so, BufferHandle dst, uint64_t dO, uint64_t size){
    if (!open("copyBuffer")) return;
    if (!use(src, Access::CopySrc, BufferUsage::CopySrc, "copyBuffer source") || !use(dst, Access::CopyDst, BufferUsage::CopyDst, "copyBuffer destination"))
        return;
    const BufState* a = dev_.buffer(src); const BufState* b = dev_.buffer(dst);
    if (so + size > a->desc.size || dO + size > b->desc.size){
        dev_.report("V02", "copy of " + n(size) + " bytes from " + bufferName(a) + " at " + n(so) + " to " + bufferName(b) + " at " + n(dO)
                           + " runs past a buffer's end");
        return;
    }
    inner->copyBuffer(src, so, dst, dO, size);
    mark(src, Access::CopySrc); mark(dst, Access::CopyDst);
}
void ValidatingList::dispatch(PipelineHandle p, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z){
    if (!open("dispatch")) return;
    const std::vector<Binding>* layout = dev_.pipeline(p);
    if (!layout){ dev_.report("V01", "dispatch of a handle that is not a live compute pipeline"); return; }
    if (layout->size() != binds.size()){
        dev_.report("V04", "dispatch with " + n(binds.size()) + " bindings for a layout of " + n(layout->size())); return; }
    if (!x || !y || !z || x > 65535 || y > 65535 || z > 65535){
        dev_.report("V05", "dispatch of " + n(x) + " x " + n(y) + " x " + n(z) + " workgroups"); return; }
    std::vector<Access> need(binds.size());
    for (std::size_t i = 0; i < binds.size(); ++i){
        const Binding k = (*layout)[i];
        need[i] = k == Binding::Uniform ? Access::Uniform : k == Binding::StorageRead ? Access::StorageRead : Access::StorageWrite;
        const BufferUsage u = k == Binding::Uniform ? BufferUsage::Uniform : BufferUsage::Storage;
        if (!use(binds[i], need[i], u, ("dispatch binding " + n(i)).c_str())) return;
    }
    inner->dispatch(p, binds, x, y, z);
    for (std::size_t i = 0; i < binds.size(); ++i) mark(binds[i], need[i]);
}
void ValidatingList::uploadTexture(TextureHandle t, const void* rgba, uint32_t w, uint32_t h){
    if (!open("uploadTexture")) return;
    const TexState* s = dev_.texture(t);
    if (!s){ dev_.report("V01", "uploadTexture to a handle that is not a live texture"); return; }
    if (!has(s->desc.usage, TextureUsage::CopyDst) || w != s->desc.width || h != s->desc.height){
        dev_.report("V11", "uploadTexture of " + n(w) + " x " + n(h) + " into texture '" + std::string(s->desc.label ? s->desc.label : "")
                           + "' of " + n(s->desc.width) + " x " + n(s->desc.height) + (has(s->desc.usage, TextureUsage::CopyDst) ? "" : " without CopyDst"));
        return;
    }
    inner->uploadTexture(t, rgba, w, h);
}
void ValidatingList::copyTextureToBuffer(TextureHandle t, BufferHandle dst){
    if (!open("copyTextureToBuffer")) return;
    const TexState* s = dev_.texture(t);
    if (!s){ dev_.report("V01", "copyTextureToBuffer from a handle that is not a live texture"); return; }
    if (!use(dst, Access::CopyDst, BufferUsage::CopyDst, "copyTextureToBuffer destination")) return;
    const uint64_t need = uint64_t(textureRowPitch(s->desc.width, s->desc.format)) * s->desc.height;
    const BufState* b = dev_.buffer(dst);
    if (!has(s->desc.usage, TextureUsage::CopySrc) || b->desc.size < need){
        dev_.report("V12", "copyTextureToBuffer needs " + n(need) + " bytes and a CopySrc texture; " + bufferName(b) + " has " + n(b->desc.size)); return; }
    inner->copyTextureToBuffer(t, dst);
    mark(dst, Access::CopyDst);
}
void ValidatingList::beginRenderPass(const RenderPassDesc& p){
    if (!open("beginRenderPass")) return;
    if (passOpen){ dev_.report("V09", "beginRenderPass inside an open render pass"); return; }
    for (uint32_t i = 0; i < p.colorCount && i < kMaxColorTargets; ++i){
        const TexState* s = dev_.texture(p.color[i].texture);
        if (!s){ dev_.report("V01", "render pass colour target " + n(i) + " is not a live texture"); return; }
        if (!has(s->desc.usage, TextureUsage::RenderTarget)){ dev_.report("V10", "render pass colour target " + n(i) + " lacks RenderTarget usage"); return; }
    }
    if (p.depth.valid()){
        const TexState* s = dev_.texture(p.depth);
        if (!s){ dev_.report("V01", "render pass depth target is not a live texture"); return; }
        if (!has(s->desc.usage, TextureUsage::RenderTarget)){ dev_.report("V10", "render pass depth target lacks RenderTarget usage"); return; }
    }
    inner->beginRenderPass(p);
    pass = p; passOpen = true;
}
void ValidatingList::draw(RasterPipelineHandle p, std::span<const RasterBind> binds, uint32_t vertexCount){
    if (!open("draw")) return;
    if (!passOpen){ dev_.report("V09", "draw outside a render pass"); return; }
    const RasterState* r = dev_.raster(p);
    if (!r){ dev_.report("V01", "draw with a handle that is not a live raster pipeline"); return; }
    bool formats = r->targetCount == pass.colorCount;
    for (uint32_t i = 0; formats && i < pass.colorCount && i < kMaxColorTargets; ++i){
        const TexState* s = dev_.texture(pass.color[i].texture);
        formats = s && s->desc.format == r->targets[i];
    }
    if (!formats){ dev_.report("V10", "draw whose pipeline targets differ from the render pass's in count or format"); return; }
    if (r->layout.size() != binds.size()){ dev_.report("V04", "draw with " + n(binds.size()) + " bindings for a layout of " + n(r->layout.size())); return; }
    for (std::size_t i = 0; i < binds.size(); ++i){
        const RasterBinding k = r->layout[i];
        if (k == RasterBinding::Texture && !dev_.texture(binds[i].texture)){ dev_.report("V01", "draw binding " + n(i) + " is not a live texture"); return; }
        if (k == RasterBinding::Sampler && !dev_.sampler(binds[i].sampler)){ dev_.report("V01", "draw binding " + n(i) + " is not a live sampler"); return; }
        if (k == RasterBinding::StorageRead && !use(binds[i].buffer, Access::StorageRead, BufferUsage::Storage, "draw binding")) return;
        if (k == RasterBinding::Uniform && !use(binds[i].buffer, Access::Uniform, BufferUsage::Uniform, "draw binding")) return;
    }
    inner->draw(p, binds, vertexCount);
    for (std::size_t i = 0; i < binds.size(); ++i){
        if (r->layout[i] == RasterBinding::StorageRead) mark(binds[i].buffer, Access::StorageRead);
        if (r->layout[i] == RasterBinding::Uniform) mark(binds[i].buffer, Access::Uniform);
    }
}
void ValidatingList::endRenderPass(){
    if (!open("endRenderPass")) return;
    if (!passOpen){ dev_.report("V09", "endRenderPass with no render pass open"); return; }
    inner->endRenderPass();
    passOpen = false;
}
}  // namespace raw::rhi::validate
