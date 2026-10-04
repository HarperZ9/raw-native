// The WebGPU backend of the RHI, through Emscripten's WebGPU port
// (emdawnwebgpu). Waits use wgpuInstanceWaitAny with JSPI, so a submission
// reads as straight-line code and the CLI's main() stays synchronous in shape.
// Private to src/rhi/webgpu/: nothing else includes webgpu.h.
//
// WebGPU synchronizes passes itself, so barrier() records nothing; each
// dispatch is its own compute pass. Pipelines use the layout WebGPU derives
// from the WGSL, and the RHI's layout is checked against each dispatch.
#include "raw/core/slot_pool.hpp"
#include "raw/rhi/rhi.hpp"
#include <webgpu/webgpu.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
namespace raw::rhi::webgpu {
namespace {
WGPUStringView sv(const char* s){ return WGPUStringView{s, WGPU_STRLEN}; }
std::string str(WGPUStringView v){ return v.data ? std::string(v.data, v.length == WGPU_STRLEN ? std::strlen(v.data) : v.length) : std::string(); }
const char* apiName(WGPUBackendType b){
    switch (b){ case WGPUBackendType_WebGPU: return "webgpu"; case WGPUBackendType_D3D12: return "d3d12";
        case WGPUBackendType_D3D11: return "d3d11"; case WGPUBackendType_Vulkan: return "vulkan";
        case WGPUBackendType_Metal: return "metal"; default: return ""; }
}
struct PipeRec { WGPUComputePipeline pipe{}; std::size_t bindCount{0}; };

class WebGpuDevice;
class WebGpuCommandList final : public CommandList {
public:
    explicit WebGpuCommandList(WebGpuDevice& d) : dev(d) {}
    void barrier(std::span<const BufferBarrier>) override {}
    void upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size) override;
    void copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z) override;
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

bool WebGpuDevice::init(std::string& err){
    WGPUInstanceFeatureName feat = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
    id.requiredFeatureCount = 1; id.requiredFeatures = &feat;
    inst = wgpuCreateInstance(&id);
    if (!inst){ err = "no WebGPU instance (navigator.gpu is absent)"; return false; }
    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    ao.powerPreference = WGPUPowerPreference_HighPerformance;
    struct Req { WebGpuDevice* d; std::string error; } req{this, {}};
    WGPURequestAdapterCallbackInfo acb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    acb.mode = WGPUCallbackMode_WaitAnyOnly; acb.userdata1 = &req;
    acb.callback = [](WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void* u, void*){
        auto* r = (Req*)u; if (s == WGPURequestAdapterStatus_Success) r->d->adapterObj = a; else r->error = "no adapter: " + str(m); };
    waitFor(wgpuInstanceRequestAdapter(inst, &ao, acb));
    if (!adapterObj){ err = req.error.empty() ? "no adapter" : req.error; return false; }
    WGPUAdapterInfo ai = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapterObj, &ai) == WGPUStatus_Success){
        info = AdapterInfo{str(ai.vendor), str(ai.architecture), str(ai.device), str(ai.description), apiName(ai.backendType), ""};
        wgpuAdapterInfoFreeMembers(ai);
    }
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView m, void*, void*){
        std::fprintf(stderr, "webgpu error: %s\n", str(m).c_str()); };
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView m, void*, void*){
        std::fprintf(stderr, "webgpu device lost: %s\n", str(m).c_str()); };
    WGPURequestDeviceCallbackInfo dcb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dcb.mode = WGPUCallbackMode_WaitAnyOnly; dcb.userdata1 = &req;
    dcb.callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void* u, void*){
        auto* r = (Req*)u; if (s == WGPURequestDeviceStatus_Success) r->d->dev = d; else r->error = "no device: " + str(m); };
    waitFor(wgpuAdapterRequestDevice(adapterObj, &dd, dcb));
    if (!dev){ err = req.error.empty() ? "no device" : req.error; return false; }
    queue = wgpuDeviceGetQueue(dev);
    return true;
}

void WebGpuCommandList::upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size){
    WGPUBuffer* b = dev.buffer(dst);
    if (!b){ fail("webgpu: upload to an unknown buffer"); return; }
    wgpuQueueWriteBuffer(dev.queue, *b, offset, data, (size_t)size);   // ordered before this list's submit
}
void WebGpuCommandList::copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size){
    WGPUBuffer* s = dev.buffer(src); WGPUBuffer* d = dev.buffer(dst);
    if (!s || !d){ fail("webgpu: copy with an unknown buffer"); return; }
    wgpuCommandEncoderCopyBufferToBuffer(enc, *s, srcOffset, *d, dstOffset, size);
}
void WebGpuCommandList::dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds,
                                 uint32_t x, uint32_t y, uint32_t z){
    PipeRec* p = dev.pipes.get(pipeline.index, pipeline.gen);
    if (!p){ fail("webgpu: dispatch with an unknown pipeline"); return; }
    if (binds.size() != p->bindCount){ fail("webgpu: dispatch binding count differs from the pipeline layout"); return; }
    std::vector<WGPUBindGroupEntry> e(binds.size());
    for (size_t i = 0; i < e.size(); ++i){
        WGPUBuffer* b = dev.buffer(binds[i]);
        if (!b){ fail("webgpu: dispatch binds an unknown buffer"); return; }
        e[i] = WGPU_BIND_GROUP_ENTRY_INIT; e[i].binding = (uint32_t)i; e[i].buffer = *b; e[i].size = WGPU_WHOLE_SIZE;
    }
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(p->pipe, 0);
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bd.layout = layout; bd.entryCount = e.size(); bd.entries = e.data();
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(dev.dev, &bd);
    wgpuBindGroupLayoutRelease(layout);
    groups.push_back(g);
    WGPUComputePassEncoder pe = wgpuCommandEncoderBeginComputePass(enc, nullptr);
    wgpuComputePassEncoderSetPipeline(pe, p->pipe);
    wgpuComputePassEncoderSetBindGroup(pe, 0, g, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pe, x, y, z);
    wgpuComputePassEncoderEnd(pe);
    wgpuComputePassEncoderRelease(pe);
}
}  // namespace
}  // namespace raw::rhi::webgpu

namespace raw::rhi {
const char* linkedBackend(){ return "webgpu"; }
Device* device(std::string& err){
    struct Once { std::unique_ptr<webgpu::WebGpuDevice> dev; std::string error; bool tried{false}; };
    static Once once;
    if (!once.tried){
        once.tried = true;
        auto d = std::make_unique<webgpu::WebGpuDevice>();
        if (d->init(once.error)) once.dev = std::move(d);
    }
    if (!once.dev) err = once.error;
    return once.dev.get();
}
}
