// Acceleration structures, refit and root-CBV pipelines for the HW ray tracing path (hw_accel.hpp).
#include "hw_accel.hpp"
#include "raw/rhi/hw.hpp"
#include "raw_hw_dxil.hpp"
#include <cstring>
namespace raw::rhi::d3d12 {
namespace {
void uavBarrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r){
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = r;
    l->ResourceBarrier(1, &b);
}
D3D12_RAYTRACING_GEOMETRY_DESC geometry(const HwAccel& a){
    D3D12_RAYTRACING_GEOMETRY_DESC g{};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    g.Triangles.VertexCount = a.triangles * 3;
    g.Triangles.VertexBuffer = {a.verts->GetGPUVirtualAddress(), 12};
    return g;
}
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomInputs(const D3D12_RAYTRACING_GEOMETRY_DESC* g, bool updatable, bool update){
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS b{};
    b.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    b.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (updatable) b.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    if (update) b.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    b.NumDescs = 1; b.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; b.pGeometryDescs = g;
    return b;
}
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topInputs(){
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS t{};
    t.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    t.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    t.NumDescs = 1; t.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    return t;
}
// Record the bottom-level build or update, then the top-level build, between timestamps 0 and 1.
bool record(HwQueue& q, HwAccel& a, bool update, double& ms, std::string& err){
    ComPtr<ID3D12GraphicsCommandList4> l4;
    if (!q.begin(err)) return false;
    if (FAILED(q.list()->QueryInterface(IID_PPV_ARGS(&l4)))){ err = "ID3D12GraphicsCommandList4 unavailable"; return false; }
    const D3D12_RAYTRACING_GEOMETRY_DESC g = geometry(a);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
    bd.Inputs = bottomInputs(&g, a.updatable, update);
    bd.DestAccelerationStructureData = a.blas->GetGPUVirtualAddress();
    bd.ScratchAccelerationStructureData = a.scratch->GetGPUVirtualAddress();
    if (update) bd.SourceAccelerationStructureData = a.blas->GetGPUVirtualAddress();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC td{};
    td.Inputs = topInputs(); td.Inputs.InstanceDescs = a.inst->GetGPUVirtualAddress();
    td.DestAccelerationStructureData = a.tlas->GetGPUVirtualAddress();
    td.ScratchAccelerationStructureData = a.scratch->GetGPUVirtualAddress();
    q.timestamp(0);
    l4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    uavBarrier(l4.Get(), a.blas.Get());
    uavBarrier(l4.Get(), a.scratch.Get());
    l4->BuildRaytracingAccelerationStructure(&td, 0, nullptr);
    uavBarrier(l4.Get(), a.tlas.Get());
    q.timestamp(1);
    if (!q.submitAndWait(2, err)) return false;
    ms = 1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond();
    return true;
}
}  // namespace

bool hwRayQueryOn(ID3D12Device* d){
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    return !hw::disabled(hw::kRayQuery) && SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5)) &&
           o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
}

bool buildAccel(HwQueue& q, const std::vector<float>& tris, const float offset[3], bool allowUpdate, HwAccel& a,
                double& ms, std::string& err){
    ComPtr<ID3D12Device5> d5;
    if (FAILED(q.device()->QueryInterface(IID_PPV_ARGS(&d5)))){ err = "ID3D12Device5 unavailable"; return false; }
    if (tris.size() < 9 || tris.size() % 9){ err = "need whole triangles"; return false; }
    a.triangles = (uint32_t)(tris.size() / 9);
    a.updatable = allowUpdate;
    for (int i = 0; i < 3; ++i) a.offset[i] = offset[i];
    if (!q.uploadBuffer(tris.data(), 4ull * tris.size(), a.verts, err)) return false;
    const D3D12_RAYTRACING_GEOMETRY_DESC g = geometry(a);
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bl = bottomInputs(&g, allowUpdate, false);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tl = topInputs();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bp{}, tp{};
    d5->GetRaytracingAccelerationStructurePrebuildInfo(&bl, &bp);
    d5->GetRaytracingAccelerationStructurePrebuildInfo(&tl, &tp);
    uint64_t scratch = bp.ScratchDataSizeInBytes > tp.ScratchDataSizeInBytes ? bp.ScratchDataSizeInBytes : tp.ScratchDataSizeInBytes;
    if (bp.UpdateScratchDataSizeInBytes > scratch) scratch = bp.UpdateScratchDataSizeInBytes;
    if (!q.accelBuffer(bp.ResultDataMaxSizeInBytes, a.blas, err) || !q.accelBuffer(tp.ResultDataMaxSizeInBytes, a.tlas, err) ||
        !q.uavBuffer(scratch, a.scratch, err)) return false;
    D3D12_RAYTRACING_INSTANCE_DESC inst{};
    inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 1.0f;
    for (int i = 0; i < 3; ++i) inst.Transform[i][3] = offset[i];
    inst.InstanceMask = 0xFF;
    inst.AccelerationStructure = a.blas->GetGPUVirtualAddress();
    if (!q.uploadBuffer(&inst, sizeof inst, a.inst, err)) return false;
    return record(q, a, false, ms, err);
}

bool refitAccel(HwQueue& q, HwAccel& a, const std::vector<float>& tris, double& ms, std::string& err){
    if (!a.updatable){ err = "the structure was built without allowUpdate"; return false; }
    if (tris.size() != 9ull * a.triangles){ err = "a refit keeps the triangle count"; return false; }
    if (!q.uploadBuffer(tris.data(), 4ull * tris.size(), a.verts, err)) return false;
    return record(q, a, true, ms, err);
}

bool cbvPipeline(ID3D12Device* dev, const char* dxilName, bool cbv, uint32_t firstUav, uint32_t uavs, bool srv,
                 CbvPipeline& out, std::string& err){
    const HwDxilBlob* blob = nullptr;
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, dxilName) == 0) blob = &b;
    if (!blob){ err = std::string("this build carries no ") + dxilName + " shader"; return false; }
    std::vector<D3D12_ROOT_PARAMETER> params;
    if (cbv){
        D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p.Descriptor.ShaderRegister = 0; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    for (uint32_t i = 0; i < uavs; ++i){
        D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p.Descriptor.ShaderRegister = firstUav + i; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    if (srv){
        D3D12_ROOT_PARAMETER p{}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p.Descriptor.ShaderRegister = 0; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = (UINT)params.size(); rd.pParameters = params.data();
    ComPtr<ID3DBlob> sig, msg;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &msg);
    if (FAILED(hr)){ err = hrError("hw rt root signature", hr); return false; }
    if (FAILED(hr = dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&out.rs)))){
        err = hrError("hw rt CreateRootSignature", hr); return false; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = out.rs.Get(); pd.CS = {blob->bytes, blob->size};
    if (FAILED(hr = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&out.pso)))){
        err = hrError(std::string("hw rt CreateComputePipelineState (") + dxilName + ")", hr); return false; }
    out.cbv = cbv; out.srv = srv; out.uavs = uavs;
    return true;
}

void cbvDispatch(ID3D12GraphicsCommandList* l, const CbvPipeline& p, D3D12_GPU_VIRTUAL_ADDRESS cbv,
                 const std::vector<ID3D12Resource*>& uavs, D3D12_GPU_VIRTUAL_ADDRESS srv, uint32_t x, uint32_t y, uint32_t z){
    l->SetComputeRootSignature(p.rs.Get());
    l->SetPipelineState(p.pso.Get());
    UINT slot = 0;
    if (p.cbv) l->SetComputeRootConstantBufferView(slot++, cbv);
    for (uint32_t i = 0; i < p.uavs; ++i) l->SetComputeRootUnorderedAccessView(slot++, uavs[i]->GetGPUVirtualAddress());
    if (p.srv) l->SetComputeRootShaderResourceView(slot++, srv);
    l->Dispatch(x, y, z);
}

bool uploadUav(HwQueue& q, const void* data, uint64_t bytes, ComPtr<ID3D12Resource>& out, std::string& err){
    static const uint32_t zero[4] = {0, 0, 0, 0};
    if (bytes == 0){ data = zero; bytes = sizeof zero; }
    ComPtr<ID3D12Resource> up;
    if (!q.uploadBuffer(data, bytes, up, err) || !q.uavBuffer(bytes, out, err) || !q.begin(err)) return false;
    q.copy(out.Get(), up.Get(), bytes);
    return q.submitAndWait(0, err);
}

bool readUav(HwQueue& q, ID3D12Resource* src, void* out, uint64_t bytes, std::string& err){
    ComPtr<ID3D12Resource> rb;
    if (!q.readbackBuffer(bytes, rb, err) || !q.begin(err)) return false;
    q.copy(rb.Get(), src, bytes);
    if (!q.submitAndWait(0, err)) return false;
    return q.readBack(rb.Get(), out, bytes, err);
}

}  // namespace raw::rhi::d3d12
