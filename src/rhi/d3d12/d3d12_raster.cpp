// The D3D12 backend's raster pipelines and render passes (RHI version 3: up to four colour
// targets, a Depth32Float target, culling, and read-only storage buffers in raster stages).
// See d3d12_device.hpp for the model; textures and samplers are in d3d12_texture.cpp.
#include "d3d12_device.hpp"
#include <cstring>
namespace raw::rhi::d3d12 {
namespace {
D3D12_COMPARISON_FUNC compareOf(CompareFunc c){
    switch (c){
        case CompareFunc::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareFunc::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareFunc::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}
// Root parameter i is binding i: a root CBV for the uniform, an SRV table for a texture,
// a sampler table for a sampler, and a root UAV (u<k>, k counting storage bindings) for a
// read-only storage buffer, the same state the compute passes keep their buffers in.
bool rootSignature(ID3D12Device* d, std::span<const RasterBinding> layout, ComPtr<ID3D12RootSignature>& out, const std::string& pass, std::string& err){
    D3D12_ROOT_PARAMETER params[8] = {};
    D3D12_DESCRIPTOR_RANGE ranges[8] = {};
    UINT cbv = 0, srv = 0, smp = 0, uav = 0;
    for (size_t i = 0; i < layout.size(); ++i){
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        if (layout[i] == RasterBinding::Uniform){ params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[i].Descriptor.ShaderRegister = cbv++; continue; }
        if (layout[i] == RasterBinding::StorageRead){ params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; params[i].Descriptor.ShaderRegister = uav++; continue; }
        const bool tex = layout[i] == RasterBinding::Texture;
        ranges[i].RangeType = tex ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[i].NumDescriptors = 1; ranges[i].BaseShaderRegister = tex ? srv++ : smp++;
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1; params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = (UINT)layout.size(); rd.pParameters = params;
    ComPtr<ID3DBlob> blob, msg;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &msg);
    if (FAILED(hr)){ err = hrError("root signature for raster pass " + pass, hr) + (msg ? ": " + std::string((const char*)msg->GetBufferPointer(), msg->GetBufferSize()) : ""); return false; }
    if (FAILED(hr = d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&out)))){ err = hrError("CreateRootSignature " + pass, hr); return false; }
    return true;
}
}  // namespace

RasterPipelineHandle D3d12Device::createRasterPipeline(const RasterPipelineDesc& desc, std::string& err){
    const std::string pass = desc.label;
    if (desc.vertex.format != ShaderFormat::Dxil || desc.fragment.format != ShaderFormat::Dxil || !desc.vertex.bytes || !desc.fragment.bytes){
        err = "raster pass " + pass + ": the D3D12 backend takes DXIL"; return {}; }
    if (desc.layout.size() > 8){ err = "raster pass " + pass + ": more than 8 bindings"; return {}; }
    if (desc.targetCount == 0 || desc.targetCount > kMaxColorTargets){ err = "raster pass " + pass + ": 1 to 4 colour targets"; return {}; }
    RasterRec r;
    r.layout.assign(desc.layout.begin(), desc.layout.end());
    if (!rootSignature(device_.Get(), desc.layout, r.rs, pass, err)) return {};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = r.rs.Get();
    pd.VS = {desc.vertex.bytes, desc.vertex.size}; pd.PS = {desc.fragment.bytes, desc.fragment.size};
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = desc.cull == CullMode::Back ? D3D12_CULL_MODE_BACK : desc.cull == CullMode::Front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_NONE;
    pd.RasterizerState.FrontCounterClockwise = TRUE;         // as WebGPU's default front face
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = desc.targetCount;
    for (uint32_t k = 0; k < desc.targetCount; ++k){
        if (isDepth(desc.targets[k])){ err = "raster pass " + pass + ": a colour target cannot be a depth format"; return {}; }
        pd.RTVFormats[k] = dxgiFormat(desc.targets[k]);
        pd.BlendState.RenderTarget[k].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    if (desc.depth.enabled){
        pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthWriteMask = desc.depth.write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc = compareOf(desc.depth.compare);
    }
    r.targetCount = desc.targetCount; r.depth = desc.depth.enabled;
    pd.SampleDesc.Count = 1;
    HRESULT hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&r.pso));
    if (FAILED(hr)){ err = hrError("CreateGraphicsPipelineState " + pass, hr); return {}; }
    auto id = rasters_.insert(std::move(r));
    return {id.index, id.gen};
}

void D3d12CommandList::beginRenderPass(const RenderPassDesc& pass){
    if (pass.colorCount == 0 || pass.colorCount > kMaxColorTargets){ fail("d3d12: a render pass has 1 to 4 colour targets"); return; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[kMaxColorTargets];
    ID3D12GraphicsCommandList* cl = dev_.list();
    uint32_t w = 0, h = 0;
    for (uint32_t k = 0; k < pass.colorCount; ++k){
        TexRec* t = dev_.texture(pass.color[k].texture);
        if (!t || t->rtv == ~0u){ fail("d3d12: render pass on a texture that is not a colour target"); return; }
        if (k && (t->w != w || t->h != h)){ fail("d3d12: render pass targets differ in size"); return; }
        w = t->w; h = t->h;
        transition(*t, D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtv[k] = cpuAt(dev_.rtvCpu, t->rtv);
        targets[k] = pass.color[k].texture;
    }
    targetCount = pass.colorCount;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    depthTarget = pass.depth;
    if (pass.depth.valid()){
        TexRec* d = dev_.texture(pass.depth);
        if (!d || d->dsv == ~0u){ fail("d3d12: render pass depth target is not a depth target"); return; }
        if (d->w != w || d->h != h){ fail("d3d12: render pass depth target differs in size"); return; }
        transition(*d, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        dsv = cpuAt(dev_.dsvCpu, d->dsv);
    }
    cl->OMSetRenderTargets(pass.colorCount, rtv, FALSE, pass.depth.valid() ? &dsv : nullptr);
    for (uint32_t k = 0; k < pass.colorCount; ++k) cl->ClearRenderTargetView(rtv[k], pass.color[k].clear, 0, nullptr);
    if (pass.depth.valid()) cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, pass.depthClear, 0, 0, nullptr);
    D3D12_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    D3D12_RECT sc{0, 0, LONG(w), LONG(h)};
    cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
}

void D3d12CommandList::draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount){
    RasterRec* r = dev_.raster(pipeline);
    if (!r){ fail("d3d12: draw with an unknown raster pipeline"); return; }
    if (!targetCount){ fail("d3d12: draw outside a render pass"); return; }
    if (r->targetCount != targetCount || r->depth != depthTarget.valid()){ fail("d3d12: the pipeline's targets differ from the render pass's"); return; }
    if (binds.size() != r->layout.size()){ fail("d3d12: draw binding count differs from the pipeline layout"); return; }
    ID3D12GraphicsCommandList* cl = dev_.list();
    ID3D12DescriptorHeap* heaps[] = {dev_.srvGpu.heap.Get(), dev_.sampGpu.heap.Get()};
    cl->SetDescriptorHeaps(2, heaps);
    cl->SetGraphicsRootSignature(r->rs.Get());
    cl->SetPipelineState(r->pso.Get());
    for (size_t i = 0; i < binds.size(); ++i){
        const RasterBinding kind = r->layout[i];
        if (kind == RasterBinding::Uniform || kind == RasterBinding::StorageRead){
            BufferRec* b = dev_.buffer(binds[i].buffer);
            if (!b){ fail("d3d12: draw binds an unknown buffer"); return; }
            if (kind == RasterBinding::Uniform) cl->SetGraphicsRootConstantBufferView((UINT)i, b->res->GetGPUVirtualAddress());
            else cl->SetGraphicsRootUnorderedAccessView((UINT)i, b->res->GetGPUVirtualAddress());
        } else if (kind == RasterBinding::Texture){
            TexRec* t = dev_.texture(binds[i].texture);
            if (!t || t->srv == ~0u){ fail("d3d12: draw binds a texture that is not sampled"); return; }
            for (uint32_t k = 0; k < targetCount; ++k) if (binds[i].texture == targets[k]){ fail("d3d12: a draw samples its own render target"); return; }
            if (binds[i].texture == depthTarget){ fail("d3d12: a draw samples its own depth target"); return; }
            transition(*t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Heap& g = dev_.srvGpu;
            if (g.next >= g.capacity){ fail("d3d12: out of shader-visible SRV slots in one submission"); return; }
            const UINT slot = g.next++;
            dev_.d3d()->CopyDescriptorsSimple(1, cpuAt(g, slot), cpuAt(dev_.srvCpu, t->srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            cl->SetGraphicsRootDescriptorTable((UINT)i, gpuAt(g, slot));
        } else {
            SampRec* s = dev_.sampler(binds[i].sampler);
            if (!s){ fail("d3d12: draw binds an unknown sampler"); return; }
            Heap& g = dev_.sampGpu;
            if (g.next >= g.capacity){ fail("d3d12: out of shader-visible sampler slots in one submission"); return; }
            const UINT slot = g.next++;
            dev_.d3d()->CopyDescriptorsSimple(1, cpuAt(g, slot), cpuAt(dev_.sampCpu, s->slot), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            cl->SetGraphicsRootDescriptorTable((UINT)i, gpuAt(g, slot));
        }
    }
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->DrawInstanced(vertexCount, 1, 0, 0);
}
void D3d12CommandList::endRenderPass(){ targetCount = 0; depthTarget = {}; for (auto& t : targets) t = {}; }
}  // namespace raw::rhi::d3d12
