#pragma once
// The WebGPU backend's classes (src/rhi/webgpu/). Private to this directory: nothing
// else includes webgpu.h. webgpu_device.cpp holds the device's setup, buffers, compute
// and submission; webgpu_raster.cpp the textures, samplers and raster pipelines
// (RHI version 2). See webgpu_device.cpp for the backend's model.
#include "raw/core/slot_pool.hpp"
#include "raw/rhi/rhi.hpp"
#include <webgpu/webgpu.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
namespace raw::rhi::webgpu {
inline WGPUStringView sv(const char* s){ return WGPUStringView{s, WGPU_STRLEN}; }
inline std::string str(WGPUStringView v){ return v.data ? std::string(v.data, v.length == WGPU_STRLEN ? std::strlen(v.data) : v.length) : std::string(); }
inline const char* apiName(WGPUBackendType b){
    switch (b){ case WGPUBackendType_WebGPU: return "webgpu"; case WGPUBackendType_D3D12: return "d3d12";
        case WGPUBackendType_D3D11: return "d3d11"; case WGPUBackendType_Vulkan: return "vulkan";
        case WGPUBackendType_Metal: return "metal"; default: return ""; }
}
struct PipeRec { WGPUComputePipeline pipe{}; std::size_t bindCount{0}; };
struct TexRec { WGPUTexture tex{}; WGPUTextureView view{}; uint32_t w{0}, h{0}; };
struct RasterRec { WGPURenderPipeline pipe{}; std::vector<RasterBinding> layout; };

class WebGpuDevice;
class WebGpuCommandList final : public CommandList {
public:
    explicit WebGpuCommandList(WebGpuDevice& d) : dev(d) {}
    void barrier(std::span<const BufferBarrier>) override {}
    void upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size) override;
    void copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z) override;
    void uploadTexture(TextureHandle dst, const void* rgba, uint32_t width, uint32_t height) override;
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst) override;
    void beginRenderPass(const RenderPassDesc& pass) override;
    void draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount) override;
    void endRenderPass() override;
    WGPURenderPassEncoder pass{};   // the open render pass
    void fail(const std::string& e){ if (error.empty()) error = e; }
    WebGpuDevice& dev;
    WGPUCommandEncoder enc{};
    std::vector<WGPUBindGroup> groups;   // released after submit
    std::string error;
};

class WebGpuDevice final : public Device {
public:
    WGPUInstance inst{}; WGPUAdapter adapterObj{}; WGPUDevice dev{}; WGPUQueue queue{};
    AdapterInfo info;
    SlotPool<WGPUBuffer> buffers;
    SlotPool<PipeRec> pipes;
    SlotPool<TexRec> textures;
    SlotPool<WGPUSampler> samplers;
    SlotPool<RasterRec> rasters;
    WebGpuCommandList cmd{*this};
    bool open{false};

    void waitFor(WGPUFuture f){ WGPUFutureWaitInfo w{f, false}; wgpuInstanceWaitAny(inst, 1, &w, UINT64_MAX); }
    bool popScope(const char* what, std::string& err){
        struct R { bool bad{false}; std::string msg; } r;
        WGPUPopErrorScopeCallbackInfo cb = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        cb.mode = WGPUCallbackMode_WaitAnyOnly; cb.userdata1 = &r;
        cb.callback = [](WGPUPopErrorScopeStatus s, WGPUErrorType t, WGPUStringView m, void* u, void*){
            auto* r = (R*)u; if (s != WGPUPopErrorScopeStatus_Success || t != WGPUErrorType_NoError){ r->bad = true; r->msg = str(m); } };
        waitFor(wgpuDevicePopErrorScope(dev, cb));
        if (r.bad){ err = std::string(what) + ": " + r.msg; return false; }
        return true;
    }
    WGPUBuffer* buffer(BufferHandle h){ return buffers.get(h.index, h.gen); }
    bool init(std::string& err);

    const char* backendName() const override { return "webgpu"; }
    const AdapterInfo& adapter() const override { return info; }
    ShaderFormat shaderFormat() const override { return ShaderFormat::Wgsl; }
    BufferHandle createBuffer(const BufferDesc& d, std::string& err) override {
        WGPUBufferUsage u = WGPUBufferUsage_None;
        if (has(d.usage, BufferUsage::Uniform)) u |= WGPUBufferUsage_Uniform;
        if (has(d.usage, BufferUsage::Storage)) u |= WGPUBufferUsage_Storage;
        if (has(d.usage, BufferUsage::CopySrc)) u |= WGPUBufferUsage_CopySrc;
        if (has(d.usage, BufferUsage::CopyDst)) u |= WGPUBufferUsage_CopyDst;
        if (has(d.usage, BufferUsage::MapRead)) u |= WGPUBufferUsage_MapRead;
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = (d.size + 3) & ~uint64_t(3); bd.usage = u; bd.label = sv(d.label);
        WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &bd);
        if (!b){ err = "webgpu: buffer creation failed"; return {}; }
        auto id = buffers.insert(b);
        return {id.index, id.gen};
    }
    void destroyBuffer(BufferHandle h) override {
        if (auto b = buffers.erase(h.index, h.gen)){ wgpuBufferDestroy(*b); wgpuBufferRelease(*b); }
    }
    PipelineHandle createComputePipeline(const ComputePipelineDesc& d, std::string& err) override {
        const std::string what = std::string("pass ") + d.label;
        if (d.code.format != ShaderFormat::Wgsl || !d.code.bytes){ err = what + ": the WebGPU backend takes WGSL"; return {}; }
        wgpuDevicePushErrorScope(dev, WGPUErrorFilter_Validation);
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = WGPUStringView{(const char*)d.code.bytes, d.code.size};
        WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        md.nextInChain = &wgsl.chain;
        WGPUShaderModule mod = wgpuDeviceCreateShaderModule(dev, &md);
        WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pd.compute.module = mod; pd.compute.entryPoint = sv("main");
        WGPUComputePipeline p = wgpuDeviceCreateComputePipeline(dev, &pd);
        wgpuShaderModuleRelease(mod);
        if (!popScope(what.c_str(), err)) return {};
        auto id = pipes.insert(PipeRec{p, d.layout.size()});
        return {id.index, id.gen};
    }
    TextureHandle createTexture(const TextureDesc& desc, std::string& err) override;
    void destroyTexture(TextureHandle texture) override;
    SamplerHandle createSampler(const SamplerDesc& desc, std::string& err) override;
    RasterPipelineHandle createRasterPipeline(const RasterPipelineDesc& desc, std::string& err) override;
    WGPUShaderModule module(const ShaderCode& code);   // null on error (inside an error scope)
    CommandList* begin(std::string& err) override {
        if (open){ err = "webgpu: a command list is already open"; return nullptr; }
        wgpuDevicePushErrorScope(dev, WGPUErrorFilter_Validation);
        cmd.enc = wgpuDeviceCreateCommandEncoder(dev, nullptr);
        cmd.error.clear();
        open = true;
        return &cmd;
    }
    // Submit, then wait for the queue, so a map after it reads finished work.
    bool submitAndWait(std::string& err) override {
        if (!open){ err = "webgpu: no command list is open"; return false; }
        open = false;
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(cmd.enc, nullptr);
        const bool recorded = cmd.error.empty();
        if (recorded) wgpuQueueSubmit(queue, 1, &cb);
        wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(cmd.enc); cmd.enc = nullptr;
        for (WGPUBindGroup g : cmd.groups) wgpuBindGroupRelease(g);
        cmd.groups.clear();
        std::string scopeErr;
        const bool scopeOk = popScope("encode and submit", scopeErr);
        if (!recorded){ err = cmd.error; return false; }
        if (!scopeOk){ err = scopeErr; return false; }
        WGPUQueueWorkDoneCallbackInfo wd = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        wd.mode = WGPUCallbackMode_WaitAnyOnly;
        wd.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*){};
        waitFor(wgpuQueueOnSubmittedWorkDone(queue, wd));
        return true;
    }
    const void* mapRead(BufferHandle h, uint64_t size, std::string& err) override {
        WGPUBuffer* b = buffer(h);
        if (!b){ err = "webgpu: mapRead of an unknown buffer"; return nullptr; }
        struct M { bool ok{false}; std::string msg; } mr;
        WGPUBufferMapCallbackInfo mcb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        mcb.mode = WGPUCallbackMode_WaitAnyOnly; mcb.userdata1 = &mr;
        mcb.callback = [](WGPUMapAsyncStatus s, WGPUStringView m, void* u, void*){
            auto* r = (M*)u; r->ok = s == WGPUMapAsyncStatus_Success; if (!r->ok) r->msg = str(m); };
        waitFor(wgpuBufferMapAsync(*b, WGPUMapMode_Read, 0, (size_t)size, mcb));
        if (!mr.ok){ err = "readback failed: " + mr.msg; return nullptr; }
        return wgpuBufferGetConstMappedRange(*b, 0, (size_t)size);
    }
    void unmap(BufferHandle h) override { if (WGPUBuffer* b = buffer(h)) wgpuBufferUnmap(*b); }
};
}  // namespace raw::rhi::webgpu
