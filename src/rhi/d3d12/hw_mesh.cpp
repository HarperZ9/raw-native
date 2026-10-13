// HW H1.3: the visibility buffer by vertex pulling or by amplification and mesh shaders, with one
// root signature, one pixel shader and one raster state (raw/rhi/hw.hpp; evidence/hw-h1-3-bounds.json).
#include "hw_queue.hpp"
#include "raw/rhi/hw.hpp"
#include "raw_hw_dxil.hpp"
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwQueue;

D3D12_SHADER_BYTECODE code(const char* name){
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, name) == 0) return {b.bytes, b.size};
    return {nullptr, 0};
}
// b0: 48 root constants; t0 positions; t1 meshlets; u0 the kept-meshlet counter.
bool rootSignature(ID3D12Device* d, ComPtr<ID3D12RootSignature>& rs, std::string& err){
    D3D12_ROOT_PARAMETER p[4] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 48};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[1].Descriptor = {0, 0};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[2].Descriptor = {1, 0};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; p[3].Descriptor = {0, 0};
    for (auto& x : p) x.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{4, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, msg;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &msg);
    if (SUCCEEDED(hr)) hr = d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs));
    if (FAILED(hr)){ err = d3d12::hrError("hw mesh root signature", hr); return false; }
    return true;
}
D3D12_RASTERIZER_DESC raster(){
    D3D12_RASTERIZER_DESC r{};
    r.FillMode = D3D12_FILL_MODE_SOLID; r.CullMode = D3D12_CULL_MODE_BACK; r.FrontCounterClockwise = TRUE;
    r.DepthClipEnable = TRUE;
    return r;
}
D3D12_DEPTH_STENCIL_DESC depthState(){
    D3D12_DEPTH_STENCIL_DESC s{};
    s.DepthEnable = TRUE; s.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; s.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return s;
}
D3D12_BLEND_DESC blend(){ D3D12_BLEND_DESC b{}; b.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; return b; }

bool vertexPso(ID3D12Device* d, ID3D12RootSignature* rs, ComPtr<ID3D12PipelineState>& out, std::string& err){
    D3D12_GRAPHICS_PIPELINE_STATE_DESC g{};
    g.pRootSignature = rs; g.VS = code("hw_mesh_vs"); g.PS = code("hw_mesh_ps");
    g.BlendState = blend(); g.SampleMask = ~0u; g.RasterizerState = raster(); g.DepthStencilState = depthState();
    g.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    g.NumRenderTargets = 1; g.RTVFormats[0] = DXGI_FORMAT_R32_UINT; g.DSVFormat = DXGI_FORMAT_D32_FLOAT; g.SampleDesc.Count = 1;
    HRESULT hr = d->CreateGraphicsPipelineState(&g, IID_PPV_ARGS(&out));
    if (FAILED(hr)){ err = d3d12::hrError("hw vertex PSO", hr); return false; }
    return true;
}
// A pipeline-state stream for the amplification and mesh stages (D3D12_PIPELINE_STATE_STREAM_DESC).
template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE T, class V> struct alignas(void*) Sub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type{T}; V v; };
struct MeshStream {
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> rs;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> as;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> ms;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> mask;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> raster;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depth;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> rtv;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> dsv;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> sample;
};
bool meshPso(ID3D12Device* d, ID3D12RootSignature* rs, bool flip, ComPtr<ID3D12PipelineState>& out, std::string& err){
    ComPtr<ID3D12Device2> d2;
    if (FAILED(d->QueryInterface(IID_PPV_ARGS(&d2)))){ err = "ID3D12Device2 unavailable"; return false; }
    MeshStream s{};
    s.rs.v = rs; s.as.v = code(flip ? "hw_mesh_as_flip" : "hw_mesh_as"); s.ms.v = code("hw_mesh_ms"); s.ps.v = code("hw_mesh_ps");
    s.blend.v = blend(); s.mask.v = ~0u; s.raster.v = raster(); s.depth.v = depthState();
    s.rtv.v.NumRenderTargets = 1; s.rtv.v.RTFormats[0] = DXGI_FORMAT_R32_UINT; s.dsv.v = DXGI_FORMAT_D32_FLOAT; s.sample.v = {1, 0};
    D3D12_PIPELINE_STATE_STREAM_DESC sd{sizeof s, &s};
    HRESULT hr = d2->CreatePipelineState(&sd, IID_PPV_ARGS(&out));
    if (FAILED(hr)){ err = d3d12::hrError("hw mesh PSO", hr); return false; }
    return true;
}
struct Targets { ComPtr<ID3D12Resource> ids, depth; ComPtr<ID3D12DescriptorHeap> rtv, dsv; };
bool targets(ID3D12Device* d, uint32_t w, uint32_t h, Targets& t, std::string& err){
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Format = DXGI_FORMAT_R32_UINT; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{}; cv.Format = DXGI_FORMAT_R32_UINT;   // cleared to 0; the pixel shader writes index + 1
    HRESULT hr = d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&t.ids));
    rd.Format = DXGI_FORMAT_D32_FLOAT; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dv{}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.DepthStencil.Depth = 1.0f;
    if (SUCCEEDED(hr)) hr = d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dv, IID_PPV_ARGS(&t.depth));
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (SUCCEEDED(hr)) hr = d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&t.rtv));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (SUCCEEDED(hr)) hr = d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&t.dsv));
    if (FAILED(hr)){ err = d3d12::hrError("hw mesh targets", hr); return false; }
    d->CreateRenderTargetView(t.ids.Get(), nullptr, t.rtv->GetCPUDescriptorHandleForHeapStart());
    d->CreateDepthStencilView(t.depth.Get(), nullptr, t.dsv->GetCPUDescriptorHandleForHeapStart());
    return true;
}
void transition(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER x{}; x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
    l->ResourceBarrier(1, &x);
}
void copyOut(ID3D12GraphicsCommandList* l, ID3D12Resource* tex, ID3D12Resource* rb, DXGI_FORMAT f, uint32_t w, uint32_t h, uint32_t pitch){
    D3D12_TEXTURE_COPY_LOCATION src{tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    dst.PlacedFootprint = {0, {f, w, h, 1, pitch}};
    l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}
// One frame: clear, draw by the chosen path between timestamps 0 and 1.
void record(HwQueue& q, ID3D12RootSignature* rs, ID3D12PipelineState* pso, const Targets& t, const VisInput& in, GeomPath path,
            ID3D12Resource* pos, ID3D12Resource* mesh, ID3D12Resource* counter){
    ID3D12GraphicsCommandList* l = q.list();
    const float zero[4] = {0, 0, 0, 0};
    const auto rtv = t.rtv->GetCPUDescriptorHandleForHeapStart(), dsv = t.dsv->GetCPUDescriptorHandleForHeapStart();
    l->ClearRenderTargetView(rtv, zero, 0, nullptr);
    l->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    const D3D12_VIEWPORT vp{0, 0, (float)in.width, (float)in.height, 0, 1};
    const D3D12_RECT sc{0, 0, (LONG)in.width, (LONG)in.height};
    l->RSSetViewports(1, &vp); l->RSSetScissorRects(1, &sc);
    l->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    l->SetGraphicsRootSignature(rs); l->SetPipelineState(pso);
    l->SetGraphicsRoot32BitConstants(0, 48, in.constants, 0);
    l->SetGraphicsRootShaderResourceView(1, pos->GetGPUVirtualAddress());
    l->SetGraphicsRootShaderResourceView(2, mesh->GetGPUVirtualAddress());
    l->SetGraphicsRootUnorderedAccessView(3, counter->GetGPUVirtualAddress());
    q.timestamp(0);
    if (path == GeomPath::Vertex){
        l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        l->DrawInstanced((UINT)(in.triangles.size() / 3), 1, 0, 0);
    } else {
        ComPtr<ID3D12GraphicsCommandList6> l6;
        if (SUCCEEDED(l->QueryInterface(IID_PPV_ARGS(&l6)))) l6->DispatchMesh((UINT)((in.meshlets.size() / 12 + 31) / 32), 1, 1);
    }
    q.timestamp(1);
}
}  // namespace

VisDraw drawVisibility(Device& dev, const VisInput& in, GeomPath path, int warmup, int repeats){
    VisDraw r;
    if (path != GeomPath::Vertex && disabled(kMeshShader)){ r.error = "mesh shaders forced off"; return r; }
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(dev).d3d();
    HwQueue q;
    ComPtr<ID3D12RootSignature> rs;
    ComPtr<ID3D12PipelineState> pso;
    Targets t;
    if (!q.init(d, D3D12_COMMAND_LIST_TYPE_DIRECT, 2, r.error) || !rootSignature(d, rs, r.error) ||
        !(path == GeomPath::Vertex ? vertexPso(d, rs.Get(), pso, r.error) : meshPso(d, rs.Get(), path == GeomPath::MeshConeFlipControl, pso, r.error)) ||
        !targets(d, in.width, in.height, t, r.error)) return r;
    const uint32_t pitch = (in.width * 4 + 255) & ~255u;
    ComPtr<ID3D12Resource> pos, mesh, counter, rbIds, rbDepth, rbCount;
    if (!q.uploadBuffer(in.triangles.data(), 4ull * in.triangles.size(), pos, r.error) ||
        !q.uploadBuffer(in.meshlets.data(), 4ull * in.meshlets.size(), mesh, r.error) || !q.uavBuffer(4, counter, r.error) ||
        !q.readbackBuffer((uint64_t)pitch * in.height, rbIds, r.error) || !q.readbackBuffer((uint64_t)pitch * in.height, rbDepth, r.error) ||
        !q.readbackBuffer(4, rbCount, r.error)) return r;
    const int total = 1 + warmup + repeats;
    for (int i = 0; i < total; ++i){
        if (!q.begin(r.error)) return r;
        record(q, rs.Get(), pso.Get(), t, in, path, pos.Get(), mesh.Get(), counter.Get());
        if (i == total - 1){   // every frame redraws the same picture; read the last one
            ID3D12GraphicsCommandList* l = q.list();
            transition(l, t.ids.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(l, t.depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
            copyOut(l, t.ids.Get(), rbIds.Get(), DXGI_FORMAT_R32_UINT, in.width, in.height, pitch);
            copyOut(l, t.depth.Get(), rbDepth.Get(), DXGI_FORMAT_D32_FLOAT, in.width, in.height, pitch);
        }
        if (!q.submitAndWait(2, r.error)) return r;
        if (i > warmup) r.ms.push_back(1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond());
    }
    // The counter in its own submission: buffers decay to COMMON between submissions, so the copy needs no transition.
    if (!q.begin(r.error)) return r;
    q.copy(rbCount.Get(), counter.Get(), 4);
    if (!q.submitAndWait(0, r.error)) return r;
    std::vector<uint8_t> a((size_t)pitch * in.height), b(a.size());
    uint32_t count = 0;
    if (!q.readBack(rbIds.Get(), a.data(), a.size(), r.error) || !q.readBack(rbDepth.Get(), b.data(), b.size(), r.error) ||
        !q.readBack(rbCount.Get(), &count, 4, r.error)) return r;
    r.ids.resize((size_t)in.width * in.height); r.depth.resize(r.ids.size());
    for (uint32_t y = 0; y < in.height; ++y){
        std::memcpy(&r.ids[(size_t)y * in.width], &a[(size_t)y * pitch], 4ull * in.width);
        std::memcpy(&r.depth[(size_t)y * in.width], &b[(size_t)y * pitch], 4ull * in.width);
    }
    for (uint32_t& id : r.ids) id -= 1;   // 0 (nothing drawn) becomes 0xFFFFFFFF
    r.keptMeshlets = path == GeomPath::Vertex ? 0 : count / (uint32_t)total;
    r.ran = true;
    return r;
}
}  // namespace raw::rhi::hw
