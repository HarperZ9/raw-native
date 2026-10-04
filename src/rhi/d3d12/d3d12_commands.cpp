// The D3D12 command list: the RHI's barriers, uploads, copies and dispatches
// recorded into one ID3D12GraphicsCommandList.
#include "d3d12_device.hpp"
#include <cstring>
namespace raw::rhi::d3d12 {
namespace {
// Every storage buffer is bound as a root UAV, read-only ones included, so both
// storage accesses share the UNORDERED_ACCESS state; a write between them is
// ordered with a UAV barrier instead of a transition.
D3D12_RESOURCE_STATES stateOf(Access a){
    switch (a){
        case Access::Uniform: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case Access::StorageRead: case Access::StorageWrite: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case Access::CopySrc: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case Access::CopyDst: return D3D12_RESOURCE_STATE_COPY_DEST;
        case Access::Undefined: break;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}
}  // namespace

void D3d12CommandList::barrier(std::span<const BufferBarrier> barriers){
    std::vector<D3D12_RESOURCE_BARRIER> v;
    for (const BufferBarrier& b : barriers){
        BufferRec* r = dev_.buffer(b.buffer);
        if (!r){ fail("d3d12: barrier on an unknown buffer"); return; }
        if (r->heap != D3D12_HEAP_TYPE_DEFAULT) continue;   // UPLOAD and READBACK heaps keep one state
        const D3D12_RESOURCE_STATES from = stateOf(b.before), to = stateOf(b.after);
        D3D12_RESOURCE_BARRIER x = {};
        if (from != to){
            x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            x.Transition.pResource = r->res.Get(); x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            x.Transition.StateBefore = from; x.Transition.StateAfter = to;
        } else if (to == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && (isWrite(b.before) || isWrite(b.after))){
            x.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            x.UAV.pResource = r->res.Get();
        } else {
            continue;
        }
        v.push_back(x);
    }
    if (!v.empty()) dev_.list()->ResourceBarrier((UINT)v.size(), v.data());
}
void D3d12CommandList::upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size){
    BufferRec* r = dev_.buffer(dst);
    if (!r){ fail("d3d12: upload to an unknown buffer"); return; }
    if (offset + size > r->size){ fail("d3d12: upload past the end of a buffer"); return; }
    if (r->heap == D3D12_HEAP_TYPE_READBACK){ fail("d3d12: upload into a MapRead buffer"); return; }
    ID3D12Resource* target = r->res.Get();
    uint64_t at = offset;
    if (r->heap == D3D12_HEAP_TYPE_DEFAULT){
        // Stage the bytes in an UPLOAD buffer and copy them on the GPU.
        ComPtr<ID3D12Resource> s;
        std::string err;
        if (!dev_.committed(size, D3D12_HEAP_TYPE_UPLOAD, s, err)){ fail(err); return; }
        staging.push_back(s);
        target = s.Get(); at = 0;
    }
    unsigned char* p = nullptr;
    D3D12_RANGE none{0, 0};
    HRESULT hr = target->Map(0, &none, (void**)&p);
    if (FAILED(hr)){ fail(hrError("upload Map", hr)); return; }
    std::memcpy(p + at, data, (size_t)size);
    target->Unmap(0, nullptr);
    if (r->heap == D3D12_HEAP_TYPE_DEFAULT) dev_.list()->CopyBufferRegion(r->res.Get(), offset, target, 0, size);
}
void D3d12CommandList::copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size){
    BufferRec* s = dev_.buffer(src); BufferRec* d = dev_.buffer(dst);
    if (!s || !d){ fail("d3d12: copy with an unknown buffer"); return; }
    dev_.list()->CopyBufferRegion(d->res.Get(), dstOffset, s->res.Get(), srcOffset, size);
}
void D3d12CommandList::dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds,
                                uint32_t x, uint32_t y, uint32_t z){
    PipeRec* p = dev_.pipeline(pipeline);
    if (!p){ fail("d3d12: dispatch with an unknown pipeline"); return; }
    if (binds.size() != p->layout.size()){ fail("d3d12: dispatch binding count differs from the pipeline layout"); return; }
    ID3D12GraphicsCommandList* cl = dev_.list();
    cl->SetComputeRootSignature(p->rs.Get());
    cl->SetPipelineState(p->pso.Get());
    for (size_t i = 0; i < binds.size(); ++i){
        BufferRec* r = dev_.buffer(binds[i]);
        if (!r){ fail("d3d12: dispatch binds an unknown buffer"); return; }
        const D3D12_GPU_VIRTUAL_ADDRESS va = r->res->GetGPUVirtualAddress();
        if (p->layout[i] == Binding::Uniform) cl->SetComputeRootConstantBufferView((UINT)i, va);
        else cl->SetComputeRootUnorderedAccessView((UINT)i, va);
    }
    cl->Dispatch(x, y, z);
}
}
