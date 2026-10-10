// The D3D12 backend's textures, samplers, raster pipelines and render passes
// (RHI version 2, ROADMAP M2 criterion 3). See d3d12_device.hpp for the model.
#include "d3d12_device.hpp"
#include <cstring>
namespace raw::rhi::d3d12 {
namespace {
constexpr DXGI_FORMAT kRgba8 = DXGI_FORMAT_R8G8B8A8_UNORM;
bool makeHeap(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT n, bool visible, Heap& h, std::string& err){
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = type; hd.NumDescriptors = n;
    hd.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&h.heap));
    if (FAILED(hr)){ err = hrError("CreateDescriptorHeap", hr); return false; }
    h.size = d->GetDescriptorHandleIncrementSize(type); h.capacity = n; h.next = 0;
    return true;
}
D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(const Heap& h, UINT i){ auto c = h.heap->GetCPUDescriptorHandleForHeapStart(); c.ptr += SIZE_T(i) * h.size; return c; }
D3D12_GPU_DESCRIPTOR_HANDLE gpuAt(const Heap& h, UINT i){ auto g = h.heap->GetGPUDescriptorHandleForHeapStart(); g.ptr += UINT64(i) * h.size; return g; }
}  // namespace

bool D3d12Device::initHeaps(std::string& err){
    ID3D12Device* d = device_.Get();
    return makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, false, srvCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64, false, sampCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false, rtvCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true, srvGpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256, true, sampGpu, err);
}

TextureHandle D3d12Device::createTexture(const TextureDesc& desc, std::string& err){
    if (desc.format != TextureFormat::RGBA8Unorm || desc.width == 0 || desc.height == 0){ err = "d3d12: textures are RGBA8 and not empty"; return {}; }
    if (!srvCpu.heap && !initHeaps(err)) return {};
    TexRec t;
    t.w = desc.width; t.h = desc.height; t.usage = desc.usage;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc.width; rd.Height = desc.height; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = kRgba8; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = has(desc.usage, TextureUsage::RenderTarget) ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t.res));
    if (FAILED(hr)){ err = hrError("CreateCommittedResource (texture)", hr); return {}; }
    if (desc.label && *desc.label){ std::wstring w(desc.label, desc.label + std::strlen(desc.label)); t.res->SetName(w.c_str()); }
    if (has(desc.usage, TextureUsage::Sampled)){
        if (srvCpu.next >= srvCpu.capacity){ err = "d3d12: out of SRV slots"; return {}; }
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = kRgba8; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
        t.srv = srvCpu.next++;
        device_->CreateShaderResourceView(t.res.Get(), &sv, cpuAt(srvCpu, t.srv));
    }
    if (has(desc.usage, TextureUsage::RenderTarget)){
        if (rtvCpu.next >= rtvCpu.capacity){ err = "d3d12: out of RTV slots"; return {}; }
        t.rtv = rtvCpu.next++;
        device_->CreateRenderTargetView(t.res.Get(), nullptr, cpuAt(rtvCpu, t.rtv));
    }
    auto id = textures_.insert(std::move(t));
    return {id.index, id.gen};
}
void D3d12Device::destroyTexture(TextureHandle h){ textures_.erase(h.index, h.gen); }

SamplerHandle D3d12Device::createSampler(const SamplerDesc& desc, std::string& err){
    if (!srvCpu.heap && !initHeaps(err)) return {};
    if (sampCpu.next >= sampCpu.capacity){ err = "d3d12: out of sampler slots"; return {}; }
    D3D12_SAMPLER_DESC sd{};
    sd.Filter = desc.filter == Filter::Nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    const D3D12_TEXTURE_ADDRESS_MODE am = desc.address == AddressMode::Repeat ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.AddressU = sd.AddressV = sd.AddressW = am;
    sd.MaxAnisotropy = 1; sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; sd.MinLOD = 0; sd.MaxLOD = D3D12_FLOAT32_MAX;
    SampRec s{sampCpu.next++};
    device_->CreateSampler(&sd, cpuAt(sampCpu, s.slot));
    auto id = samplers_.insert(s);
    return {id.index, id.gen};
}

RasterPipelineHandle D3d12Device::createRasterPipeline(const RasterPipelineDesc& desc, std::string& err){
    const std::string pass = desc.label;
    if (desc.vertex.format != ShaderFormat::Dxil || desc.fragment.format != ShaderFormat::Dxil || !desc.vertex.bytes || !desc.fragment.bytes){
        err = "raster pass " + pass + ": the D3D12 backend takes DXIL"; return {}; }
    if (desc.layout.size() > 8){ err = "raster pass " + pass + ": more than 8 bindings"; return {}; }
    D3D12_ROOT_PARAMETER params[8] = {};
    D3D12_DESCRIPTOR_RANGE ranges[8] = {};
    UINT cbv = 0, srv = 0, smp = 0;
    for (size_t i = 0; i < desc.layout.size(); ++i){
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        if (desc.layout[i] == RasterBinding::Uniform){
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[i].Descriptor.ShaderRegister = cbv++;
            continue;
        }
        const bool tex = desc.layout[i] == RasterBinding::Texture;
        ranges[i].RangeType = tex ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[i].NumDescriptors = 1; ranges[i].BaseShaderRegister = tex ? srv++ : smp++;
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1; params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = (UINT)desc.layout.size(); rd.pParameters = params;
    ComPtr<ID3DBlob> blob, msg;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &msg);
    if (FAILED(hr)){ err = hrError("root signature for raster pass " + pass, hr) + (msg ? ": " + std::string((const char*)msg->GetBufferPointer(), msg->GetBufferSize()) : ""); return {}; }
    RasterRec r;
    r.layout.assign(desc.layout.begin(), desc.layout.end());
    if (FAILED(hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&r.rs)))){
        err = hrError("CreateRootSignature " + pass, hr); return {}; }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = r.rs.Get();
    pd.VS = {desc.vertex.bytes, desc.vertex.size}; pd.PS = {desc.fragment.bytes, desc.fragment.size};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1; pd.RTVFormats[0] = kRgba8; pd.SampleDesc.Count = 1;
    if (FAILED(hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&r.pso)))){
        err = hrError("CreateGraphicsPipelineState " + pass, hr); return {}; }
    auto id = rasters_.insert(std::move(r));
    return {id.index, id.gen};
}

void D3d12CommandList::transition(TexRec& t, D3D12_RESOURCE_STATES to){
    if (t.state == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res.Get(); b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t.state; b.Transition.StateAfter = to;
    dev_.list()->ResourceBarrier(1, &b);
    t.state = to;
}
void D3d12CommandList::uploadTexture(TextureHandle dst, const void* rgba, uint32_t width, uint32_t height){
    TexRec* t = dev_.texture(dst);
    if (!t){ fail("d3d12: upload to an unknown texture"); return; }
    if (width != t->w || height != t->h){ fail("d3d12: texture upload size differs from the texture"); return; }
    D3D12_RESOURCE_DESC rd = t->res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 total = 0;
    dev_.d3d()->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &total);
    ComPtr<ID3D12Resource> s; std::string err;
    if (!dev_.committed(total, D3D12_HEAP_TYPE_UPLOAD, s, err)){ fail(err); return; }
    staging.push_back(s);
    unsigned char* p = nullptr; D3D12_RANGE none{0, 0};
    HRESULT hr = s->Map(0, &none, (void**)&p);
    if (FAILED(hr)){ fail(hrError("texture upload Map", hr)); return; }
    for (uint32_t y = 0; y < height; ++y) std::memcpy(p + fp.Offset + size_t(y) * fp.Footprint.RowPitch, (const unsigned char*)rgba + size_t(y) * width * 4, size_t(width) * 4);
    s->Unmap(0, nullptr);
    transition(*t, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION d{}, src{};
    d.pResource = t->res.Get(); d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    src.pResource = s.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    dev_.list()->CopyTextureRegion(&d, 0, 0, 0, &src, nullptr);
}
void D3d12CommandList::copyTextureToBuffer(TextureHandle src, BufferHandle dst){
    TexRec* t = dev_.texture(src); BufferRec* b = dev_.buffer(dst);
    if (!t || !b){ fail("d3d12: texture copy with an unknown texture or buffer"); return; }
    const uint64_t need = uint64_t(textureRowPitch(t->w)) * t->h;
    if (b->size < need){ fail("d3d12: texture copy into a buffer that is too small"); return; }
    transition(*t, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION d{}, s{};
    s.pResource = t->res.Get(); s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    d.pResource = b->res.Get(); d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    d.PlacedFootprint.Offset = 0;
    d.PlacedFootprint.Footprint = {kRgba8, t->w, t->h, 1, textureRowPitch(t->w)};
    dev_.list()->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
}
void D3d12CommandList::beginRenderPass(const RenderPassDesc& pass){
    TexRec* t = dev_.texture(pass.target);
    if (!t || t->rtv == ~0u){ fail("d3d12: render pass on a texture that is not a render target"); return; }
    transition(*t, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto rtv = cpuAt(dev_.rtvCpu, t->rtv);
    ID3D12GraphicsCommandList* cl = dev_.list();
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cl->ClearRenderTargetView(rtv, pass.clear, 0, nullptr);
    D3D12_VIEWPORT vp{0, 0, float(t->w), float(t->h), 0, 1};
    D3D12_RECT sc{0, 0, LONG(t->w), LONG(t->h)};
    cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
    target = pass.target;
}
void D3d12CommandList::draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount){
    RasterRec* r = dev_.raster(pipeline);
    if (!r){ fail("d3d12: draw with an unknown raster pipeline"); return; }
    if (!target.valid()){ fail("d3d12: draw outside a render pass"); return; }
    if (binds.size() != r->layout.size()){ fail("d3d12: draw binding count differs from the pipeline layout"); return; }
    ID3D12GraphicsCommandList* cl = dev_.list();
    ID3D12DescriptorHeap* heaps[] = {dev_.srvGpu.heap.Get(), dev_.sampGpu.heap.Get()};
    cl->SetDescriptorHeaps(2, heaps);
    cl->SetGraphicsRootSignature(r->rs.Get());
    cl->SetPipelineState(r->pso.Get());
    for (size_t i = 0; i < binds.size(); ++i){
        if (r->layout[i] == RasterBinding::Uniform){
            BufferRec* b = dev_.buffer(binds[i].buffer);
            if (!b){ fail("d3d12: draw binds an unknown uniform buffer"); return; }
            cl->SetGraphicsRootConstantBufferView((UINT)i, b->res->GetGPUVirtualAddress());
        } else if (r->layout[i] == RasterBinding::Texture){
            TexRec* t = dev_.texture(binds[i].texture);
            if (!t || t->srv == ~0u){ fail("d3d12: draw binds a texture that is not sampled"); return; }
            if (binds[i].texture == target){ fail("d3d12: a draw samples its own render target"); return; }
            transition(*t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
void D3d12CommandList::endRenderPass(){ target = {}; }
}  // namespace raw::rhi::d3d12
