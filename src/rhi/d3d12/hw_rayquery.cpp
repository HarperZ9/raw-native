// HW H1.1: closest hits by inline ray query on D3D12 (raw/rhi/hw.hpp; bounds in
// evidence/hw-h1-1-bounds.json). One bottom-level structure over every triangle, one
// instance, built and traced on the hw module's own compute queue with timestamps.
#include "hw_queue.hpp"
#include "raw/rhi/hw.hpp"
#include "raw_hw_dxil.hpp"
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwPipeline;
using d3d12::HwQueue;

bool rayQueryOn(ID3D12Device* d){
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    return !disabled(kRayQuery) && SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5)) &&
           o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
}
void uavBarrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r){
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = r;
    l->ResourceBarrier(1, &b);
}

// The structures and the buffers that must outlive the trace.
struct Scene {
    ComPtr<ID3D12Resource> verts, inst, blas, tlas, scratch;
};
bool buildScene(HwQueue& q, const std::vector<float>& tris, const float offset[3], Scene& s, double& ms, std::string& err){
    ComPtr<ID3D12Device5> d5;
    ComPtr<ID3D12GraphicsCommandList4> l4;
    if (FAILED(q.device()->QueryInterface(IID_PPV_ARGS(&d5)))){ err = "ID3D12Device5 unavailable"; return false; }
    if (!q.uploadBuffer(tris.data(), 4ull * tris.size(), s.verts, err)) return false;
    D3D12_RAYTRACING_GEOMETRY_DESC g{};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    g.Triangles.VertexCount = (UINT)(tris.size() / 3);
    g.Triangles.VertexBuffer = {s.verts->GetGPUVirtualAddress(), 12};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bl{};
    bl.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bl.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    bl.NumDescs = 1; bl.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; bl.pGeometryDescs = &g;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bp{}, tp{};
    d5->GetRaytracingAccelerationStructurePrebuildInfo(&bl, &bp);
    D3D12_RAYTRACING_INSTANCE_DESC inst{};
    inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 1.0f;
    for (int i = 0; i < 3; ++i) inst.Transform[i][3] = offset[i];
    inst.InstanceMask = 0xFF;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tl{};
    tl.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tl.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tl.NumDescs = 1; tl.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    d5->GetRaytracingAccelerationStructurePrebuildInfo(&tl, &tp);
    const uint64_t scratch = bp.ScratchDataSizeInBytes > tp.ScratchDataSizeInBytes ? bp.ScratchDataSizeInBytes : tp.ScratchDataSizeInBytes;
    if (!q.accelBuffer(bp.ResultDataMaxSizeInBytes, s.blas, err) || !q.accelBuffer(tp.ResultDataMaxSizeInBytes, s.tlas, err) ||
        !q.uavBuffer(scratch, s.scratch, err)) return false;
    inst.AccelerationStructure = s.blas->GetGPUVirtualAddress();
    if (!q.uploadBuffer(&inst, sizeof inst, s.inst, err)) return false;
    tl.InstanceDescs = s.inst->GetGPUVirtualAddress();
    if (!q.begin(err)) return false;
    if (FAILED(q.list()->QueryInterface(IID_PPV_ARGS(&l4)))){ err = "ID3D12GraphicsCommandList4 unavailable"; return false; }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
    bd.Inputs = bl; bd.DestAccelerationStructureData = s.blas->GetGPUVirtualAddress();
    bd.ScratchAccelerationStructureData = s.scratch->GetGPUVirtualAddress();
    q.timestamp(0);
    l4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    uavBarrier(l4.Get(), s.blas.Get());
    uavBarrier(l4.Get(), s.scratch.Get());
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC td{};
    td.Inputs = tl; td.DestAccelerationStructureData = s.tlas->GetGPUVirtualAddress();
    td.ScratchAccelerationStructureData = s.scratch->GetGPUVirtualAddress();
    l4->BuildRaytracingAccelerationStructure(&td, 0, nullptr);
    uavBarrier(l4.Get(), s.tlas.Get());
    q.timestamp(1);
    if (!q.submitAndWait(2, err)) return false;
    ms = 1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond();
    return true;
}
}  // namespace

TraceResult traceRayQuery(Device& dev, const std::vector<float>& tris, const std::vector<RayIn>& rays,
                          const TraceOptions& opt, std::vector<HitOut>& hits){
    TraceResult r;
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(dev).d3d();
    if (!rayQueryOn(d)){ r.error = disabled(kRayQuery) ? "ray query forced off" : "the device has no DXR tier 1.1"; return r; }
    if (rays.empty() || tris.size() < 9 || tris.size() % 9){ r.error = "need at least one ray and whole triangles"; return r; }
    HwQueue q;
    if (!q.init(d, D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, r.error)) return r;
    const HwDxilBlob* blob = nullptr;
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, "hw_rayquery") == 0) blob = &b;
    HwPipeline p;
    if (!blob || !q.pipeline(blob->bytes, blob->size, 2, 1, true, p, r.error)) return r;
    Scene s;
    if (!buildScene(q, tris, opt.offset, s, r.buildMs, r.error)) return r;
    const uint64_t rayBytes = sizeof(RayIn) * rays.size(), hitBytes = 16ull * rays.size();
    ComPtr<ID3D12Resource> up, rayBuf, hitBuf, rb;
    if (!q.uploadBuffer(rays.data(), rayBytes, up, r.error) || !q.uavBuffer(rayBytes, rayBuf, r.error) ||
        !q.uavBuffer(hitBytes, hitBuf, r.error) || !q.readbackBuffer(hitBytes, rb, r.error)) return r;
    if (!q.begin(r.error)) return r;
    q.copy(rayBuf.Get(), up.Get(), rayBytes);
    if (!q.submitAndWait(0, r.error)) return r;
    const uint32_t count = (uint32_t)rays.size(), groups = (count + 63) / 64;
    const int total = 1 + opt.warmup + opt.repeats;
    for (int i = 0; i < total; ++i){
        if (!q.begin(r.error)) return r;
        q.timestamp(0);
        q.dispatch(p, {rayBuf.Get(), hitBuf.Get()}, &count, s.tlas->GetGPUVirtualAddress(), groups, 1, 1);
        q.timestamp(1);
        if (!q.submitAndWait(2, r.error)) return r;
        if (i > opt.warmup) r.traceMs.push_back(1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond());
    }
    if (!q.begin(r.error)) return r;
    q.copy(rb.Get(), hitBuf.Get(), hitBytes);
    if (!q.submitAndWait(0, r.error)) return r;
    std::vector<float> raw(4 * rays.size());
    if (!q.readBack(rb.Get(), raw.data(), hitBytes, r.error)) return r;
    hits.resize(rays.size());
    for (size_t i = 0; i < rays.size(); ++i){
        int32_t tri; std::memcpy(&tri, &raw[4 * i + 3], 4);
        hits[i] = {raw[4 * i], raw[4 * i + 1], raw[4 * i + 2], tri};
    }
    r.ran = true;
    return r;
}
}  // namespace raw::rhi::hw
