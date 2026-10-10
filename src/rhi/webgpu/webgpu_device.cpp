// The WebGPU backend of the RHI, through Emscripten's WebGPU port
// (emdawnwebgpu). Waits use wgpuInstanceWaitAny with JSPI, so a submission
// reads as straight-line code and the CLI's main() stays synchronous in shape.
// Private to src/rhi/webgpu/: nothing else includes webgpu.h.
//
// WebGPU synchronizes passes itself, so barrier() records nothing; each
// dispatch is its own compute pass. Pipelines use the layout WebGPU derives
// from the WGSL, and the RHI's layout is checked against each dispatch.
#include "webgpu_device.hpp"
namespace raw::rhi::webgpu {
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
