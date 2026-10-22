// GPU timestamps for the D3D12 backend (evidence/gpu-timestamps-bounds.json): a TIMESTAMP query
// heap, resolved into a readback buffer when the list is submitted (d3d12_device.cpp).
#include "d3d12_device.hpp"
#include <algorithm>
namespace raw::rhi::d3d12 {
ID3D12QueryHeap* D3d12Device::timestampHeap(std::string& err){
    if (tsHeap_) return tsHeap_.Get();
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = kMaxTimestamps;
    HRESULT hr = device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&tsHeap_));
    if (FAILED(hr)){ err = hrError("CreateQueryHeap", hr); tsHeap_.Reset(); return nullptr; }
    if (!committed(uint64_t(kMaxTimestamps) * 8, D3D12_HEAP_TYPE_READBACK, tsReadback_, err)){ tsHeap_.Reset(); return nullptr; }
    return tsHeap_.Get();
}
bool D3d12CommandList::timestamp(uint32_t slot){
    if (slot >= kMaxTimestamps){ fail("d3d12: timestamp slot out of range"); return false; }
    std::string err;
    ID3D12QueryHeap* h = dev_.timestampHeap(err);
    if (!h){ fail(err); return false; }
    dev_.list()->EndQuery(h, D3D12_QUERY_TYPE_TIMESTAMP, slot);
    timestampsUsed = std::max(timestampsUsed, slot + 1);
    return true;
}
bool D3d12Device::timestampSeconds(uint32_t count, std::vector<double>& out, std::string& err){
    out.clear();
    if (!tsHeap_ || count == 0 || count > tsResolved_){ err = "d3d12: those timestamp slots were not written in the last submission"; return false; }
    UINT64 freq = 0;
    HRESULT hr = queue_->GetTimestampFrequency(&freq);
    if (FAILED(hr) || freq == 0){ err = hrError("GetTimestampFrequency", hr); return false; }
    void* p = nullptr;
    const D3D12_RANGE r{0, SIZE_T(count) * 8};
    if (FAILED(hr = tsReadback_->Map(0, &r, &p))){ err = hrError("timestamp readback Map", hr); return false; }
    const UINT64* t = static_cast<const UINT64*>(p);
    for (uint32_t i = 0; i < count; ++i) out.push_back(double(t[i]) / double(freq));
    const D3D12_RANGE none{0, 0};
    tsReadback_->Unmap(0, &none);
    return true;
}

}  // namespace raw::rhi::d3d12
