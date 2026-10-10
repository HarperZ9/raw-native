// The HW workstream's own D3D12 queue, pipelines and timestamps (see hw_queue.hpp).
#include "hw_queue.hpp"
#include <cstring>
namespace raw::rhi::d3d12 {
namespace {
bool buffer(ID3D12Device* dev, uint64_t size, D3D12_HEAP_TYPE heap, ComPtr<ID3D12Resource>& out, std::string& err, bool accel = false){
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size < 256 ? 256 : (size + 255) & ~uint64_t(255);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = heap == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const D3D12_RESOURCE_STATES st = accel ? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
                                   : heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                   : heap == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST
                                   : D3D12_RESOURCE_STATE_COMMON;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, IID_PPV_ARGS(&out));
    if (FAILED(hr)){ err = hrError("hw buffer (" + std::to_string(size) + " bytes)", hr); return false; }
    return true;
}
}  // namespace

bool HwQueue::init(ID3D12Device* dev, D3D12_COMMAND_LIST_TYPE type, uint32_t maxTimestamps, std::string& err){
    dev_ = dev; type_ = type; maxStamps_ = maxTimestamps;
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = type;
    HRESULT hr;
    if (FAILED(hr = dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)))){ err = hrError("hw CreateCommandQueue", hr); return false; }
    if (FAILED(hr = dev->CreateCommandAllocator(type, IID_PPV_ARGS(&alloc_)))){ err = hrError("hw CreateCommandAllocator", hr); return false; }
    if (FAILED(hr = dev->CreateCommandList(0, type, alloc_.Get(), nullptr, IID_PPV_ARGS(&list_)))){ err = hrError("hw CreateCommandList", hr); return false; }
    if (FAILED(hr = list_->Close())){ err = hrError("hw list Close", hr); return false; }
    if (FAILED(hr = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))){ err = hrError("hw CreateFence", hr); return false; }
    if (!(event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr))){ err = "hw CreateEvent failed"; return false; }
    if (maxTimestamps){
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = maxTimestamps;
        if (FAILED(hr = dev->CreateQueryHeap(&qh, IID_PPV_ARGS(&queries_)))){ err = hrError("hw CreateQueryHeap", hr); return false; }
        if (!buffer(dev, 8ull * maxTimestamps, D3D12_HEAP_TYPE_READBACK, stamps_, err)) return false;
        if (FAILED(hr = queue_->GetTimestampFrequency(&freq_))){ err = hrError("GetTimestampFrequency", hr); return false; }
    }
    return true;
}

bool HwQueue::pipeline(const void* dxil, size_t size, uint32_t uavs, uint32_t constants, bool srv, HwPipeline& out, std::string& err){
    std::vector<D3D12_ROOT_PARAMETER> params;
    if (constants){
        D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0; p.Constants.Num32BitValues = constants; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    for (uint32_t i = 0; i < uavs; ++i){
        D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p.Descriptor.ShaderRegister = i; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    if (srv){
        D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p.Descriptor.ShaderRegister = 0; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = (UINT)params.size(); rd.pParameters = params.data();
    ComPtr<ID3DBlob> blob, msg;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &msg);
    if (FAILED(hr)){ err = hrError("hw root signature", hr); return false; }
    if (FAILED(hr = dev_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&out.rs)))){
        err = hrError("hw CreateRootSignature", hr); return false; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = out.rs.Get(); pd.CS = {dxil, size};
    if (FAILED(hr = dev_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&out.pso)))){
        err = hrError("hw CreateComputePipelineState", hr); return false; }
    out.uavs = uavs; out.constants = constants; out.srv = srv;
    return true;
}

bool HwQueue::uavBuffer(uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err){ return buffer(dev_, size, D3D12_HEAP_TYPE_DEFAULT, out, err); }
bool HwQueue::accelBuffer(uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err){ return buffer(dev_, size, D3D12_HEAP_TYPE_DEFAULT, out, err, true); }
bool HwQueue::readbackBuffer(uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err){ return buffer(dev_, size, D3D12_HEAP_TYPE_READBACK, out, err); }
bool HwQueue::uploadBuffer(const void* data, uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err){
    if (!buffer(dev_, size, D3D12_HEAP_TYPE_UPLOAD, out, err)) return false;
    void* p = nullptr; D3D12_RANGE none{0, 0};
    HRESULT hr = out->Map(0, &none, &p);
    if (FAILED(hr)){ err = hrError("hw upload Map", hr); return false; }
    std::memcpy(p, data, size);
    out->Unmap(0, nullptr);
    return true;
}

bool HwQueue::begin(std::string& err){
    HRESULT hr = alloc_->Reset();
    if (SUCCEEDED(hr)) hr = list_->Reset(alloc_.Get(), nullptr);
    if (FAILED(hr)){ err = hrError("hw list Reset", hr); return false; }
    return true;
}
void HwQueue::dispatch(const HwPipeline& p, const std::vector<ID3D12Resource*>& uavs, const uint32_t* constants,
                       D3D12_GPU_VIRTUAL_ADDRESS srv, uint32_t x, uint32_t y, uint32_t z){
    list_->SetComputeRootSignature(p.rs.Get());
    list_->SetPipelineState(p.pso.Get());
    UINT slot = 0;
    if (p.constants) list_->SetComputeRoot32BitConstants(slot++, p.constants, constants, 0);
    for (uint32_t i = 0; i < p.uavs; ++i) list_->SetComputeRootUnorderedAccessView(slot++, uavs[i]->GetGPUVirtualAddress());
    if (p.srv) list_->SetComputeRootShaderResourceView(slot++, srv);
    list_->Dispatch(x, y, z);
}
void HwQueue::uavBarrier(){
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = nullptr;
    list_->ResourceBarrier(1, &b);
}
void HwQueue::copy(ID3D12Resource* dst, ID3D12Resource* src, uint64_t size){ list_->CopyBufferRegion(dst, 0, src, 0, size); }
void HwQueue::timestamp(uint32_t index){ if (queries_ && index < maxStamps_) list_->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index); }

bool HwQueue::submitAndWait(uint32_t count, std::string& err){
    if (count && queries_) list_->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, count, stamps_.Get(), 0);
    HRESULT hr = list_->Close();
    if (FAILED(hr)){ err = hrError("hw list Close", hr); return false; }
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const uint64_t v = ++fenceValue_;
    if (FAILED(hr = queue_->Signal(fence_.Get(), v))){ err = hrError("hw Signal", hr); return false; }
    if (fence_->GetCompletedValue() < v){
        if (FAILED(hr = fence_->SetEventOnCompletion(v, event_))){ err = hrError("hw SetEventOnCompletion", hr); return false; }
        WaitForSingleObject(event_, INFINITE);
    }
    if (FAILED(hr = dev_->GetDeviceRemovedReason())){ err = hrError("the D3D12 device was removed", hr); return false; }
    keep_.clear();
    resolved_.assign(count, 0);
    if (count && queries_) return readBack(stamps_.Get(), resolved_.data(), 8ull * count, err);
    return true;
}
uint64_t HwQueue::ticks(uint32_t from, uint32_t to) const {
    if (from >= resolved_.size() || to >= resolved_.size() || resolved_[to] < resolved_[from]) return 0;
    return resolved_[to] - resolved_[from];
}
bool HwQueue::readBack(ID3D12Resource* rb, void* out, uint64_t size, std::string& err){
    void* p = nullptr; D3D12_RANGE r{0, (SIZE_T)size};
    HRESULT hr = rb->Map(0, &r, &p);
    if (FAILED(hr)){ err = hrError("hw readback Map", hr); return false; }
    std::memcpy(out, p, size);
    D3D12_RANGE none{0, 0}; rb->Unmap(0, &none);
    return true;
}

}  // namespace raw::rhi::d3d12
