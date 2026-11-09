#pragma once
// Hardware acceleration structures and the root-CBV pipelines of the HW ray tracing path (private
// to src/rhi/d3d12/; the public API is raw/rhi/hw_rt.hpp, the contract docs/hw-rt.md). One bottom-level
// structure over a triangle soup and one instance in a top-level structure, built on an HwQueue,
// refittable in place when built with allowUpdate.
#include "hw_queue.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rhi::d3d12 {

struct HwAccel {
    ComPtr<ID3D12Resource> verts, inst, blas, tlas, scratch;
    uint32_t triangles{0};
    bool updatable{false};
    float offset[3]{0, 0, 0};
    D3D12_GPU_VIRTUAL_ADDRESS address() const { return tlas ? tlas->GetGPUVirtualAddress() : 0; }
};

// True when the device offers DXR tier 1.1 and RAW_NATIVE_HW_DISABLE does not name ray_query.
bool hwRayQueryOn(ID3D12Device* d);

// Build over tris (9 floats a triangle, opaque, no index buffer). offset moves the instance (the
// planted-offset control). ms: GPU time of both builds.
bool buildAccel(HwQueue& q, const std::vector<float>& tris, const float offset[3], bool allowUpdate, HwAccel& a,
                double& ms, std::string& err);
// Refit a structure built with allowUpdate to new positions of the same triangles: the bottom
// level updated in place, the top level rebuilt. ms: GPU time of both.
bool refitAccel(HwQueue& q, HwAccel& a, const std::vector<float>& tris, double& ms, std::string& err);

// A compute pipeline whose root signature is: a root CBV at b0 (when cbv), root UAVs
// u[firstUav, firstUav + uavs), then a root SRV at t0 (when srv).
struct CbvPipeline {
    ComPtr<ID3D12RootSignature> rs;
    ComPtr<ID3D12PipelineState> pso;
    bool cbv{false}, srv{false};
    uint32_t uavs{0};
};
bool cbvPipeline(ID3D12Device* dev, const char* dxilName, bool cbv, uint32_t firstUav, uint32_t uavs, bool srv,
                 CbvPipeline& out, std::string& err);
void cbvDispatch(ID3D12GraphicsCommandList* l, const CbvPipeline& p, D3D12_GPU_VIRTUAL_ADDRESS cbv,
                 const std::vector<ID3D12Resource*>& uavs, D3D12_GPU_VIRTUAL_ADDRESS srv, uint32_t x, uint32_t y, uint32_t z);

// A DEFAULT-heap UAV buffer filled from data through a staging copy (submitted and waited here).
bool uploadUav(HwQueue& q, const void* data, uint64_t bytes, ComPtr<ID3D12Resource>& out, std::string& err);
// Read a UAV buffer back (copy, submit, wait, map).
bool readUav(HwQueue& q, ID3D12Resource* src, void* out, uint64_t bytes, std::string& err);

}  // namespace raw::rhi::d3d12
