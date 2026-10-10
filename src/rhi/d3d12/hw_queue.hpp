#pragma once
// The HW workstream's own submission path on the RHI's D3D12 device (private to
// src/rhi/d3d12/). A queue of its own type (compute or direct), one allocator and
// list, a fence, and a timestamp query heap with a read-back buffer. Pipelines here
// take root UAVs, an optional root SRV and root constants, so no descriptor heaps
// are needed. This is H1's measurement harness and the first piece of H3's owned
// command submission.
#include "d3d12_device.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rhi::d3d12 {

struct HwPipeline {
    ComPtr<ID3D12RootSignature> rs;
    ComPtr<ID3D12PipelineState> pso;
    uint32_t uavs{0}, constants{0};
    bool srv{false};   // root SRV t0 (an acceleration structure or a buffer)
};

class HwQueue {
public:
    // type: D3D12_COMMAND_LIST_TYPE_COMPUTE or _DIRECT. maxTimestamps: query heap size.
    bool init(ID3D12Device* dev, D3D12_COMMAND_LIST_TYPE type, uint32_t maxTimestamps, std::string& err);
    // A compute pipeline: root constants at b0 (when constants > 0), root UAVs u0..u(uavs-1),
    // then a root SRV t0 when srv is true. Fails with the HRESULT when the device refuses it.
    bool pipeline(const void* dxil, size_t size, uint32_t uavs, uint32_t constants, bool srv, HwPipeline& out, std::string& err);
    // Buffers: a DEFAULT-heap UAV buffer, an UPLOAD buffer filled from data, a READBACK buffer.
    bool uavBuffer(uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err);
    bool uploadBuffer(const void* data, uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err);
    bool readbackBuffer(uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err);
    // A DEFAULT-heap UAV buffer created in the RAYTRACING_ACCELERATION_STRUCTURE state (H1.1).
    bool accelBuffer(uint64_t size, ComPtr<ID3D12Resource>& out, std::string& err);
    ID3D12Device* device(){ return dev_; }

    bool begin(std::string& err);
    ID3D12GraphicsCommandList* list(){ return list_.Get(); }
    void dispatch(const HwPipeline& p, const std::vector<ID3D12Resource*>& uavs, const uint32_t* constants,
                  D3D12_GPU_VIRTUAL_ADDRESS srv, uint32_t x, uint32_t y, uint32_t z);
    void uavBarrier();
    void copy(ID3D12Resource* dst, ID3D12Resource* src, uint64_t size);
    void timestamp(uint32_t index);
    // Close, execute, signal, wait; resolves timestamps [0, count) into the read-back buffer.
    bool submitAndWait(uint32_t timestampCount, std::string& err);
    // Ticks between two resolved timestamps of the last submission.
    uint64_t ticks(uint32_t from, uint32_t to) const;
    double ticksPerSecond() const { return (double)freq_; }
    bool readBack(ID3D12Resource* rb, void* out, uint64_t size, std::string& err);

private:
    ID3D12Device* dev_{nullptr};
    D3D12_COMMAND_LIST_TYPE type_{D3D12_COMMAND_LIST_TYPE_COMPUTE};
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_{};
    uint64_t fenceValue_{0};
    ComPtr<ID3D12QueryHeap> queries_;
    ComPtr<ID3D12Resource> stamps_;
    uint32_t maxStamps_{0};
    uint64_t freq_{0};
    std::vector<uint64_t> resolved_;
    std::vector<ComPtr<ID3D12Resource>> keep_;   // staging kept alive until the submission ends
};

}  // namespace raw::rhi::d3d12
