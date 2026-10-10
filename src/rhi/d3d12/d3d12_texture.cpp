// The D3D12 backend's textures, samplers, texture uploads and read-back (RHI versions 2
// and 3). See d3d12_device.hpp for the model.
#include "d3d12_device.hpp"
#include <cstring>
namespace raw::rhi::d3d12 {
namespace {
bool makeHeap(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT n, bool visible, Heap& h, std::string& err){
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = type; hd.NumDescriptors = n;
    hd.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&h.heap));
    if (FAILED(hr)){ err = hrError("CreateDescriptorHeap", hr); return false; }
    h.size = d->GetDescriptorHandleIncrementSize(type); h.capacity = n; h.next = 0;
    return true;
}
}  // namespace

DXGI_FORMAT dxgiFormat(TextureFormat f){
    switch (f){
        case TextureFormat::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::RGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case TextureFormat::R32Float: return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::Depth32Float: return DXGI_FORMAT_D32_FLOAT;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}
D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(const Heap& h, UINT i){ auto c = h.heap->GetCPUDescriptorHandleForHeapStart(); c.ptr += SIZE_T(i) * h.size; return c; }
D3D12_GPU_DESCRIPTOR_HANDLE gpuAt(const Heap& h, UINT i){ auto g = h.heap->GetGPUDescriptorHandleForHeapStart(); g.ptr += UINT64(i) * h.size; return g; }

bool D3d12Device::initHeaps(std::string& err){
    ID3D12Device* d = device_.Get();
    return makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, false, srvCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64, false, sampCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false, rtvCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 32, false, dsvCpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true, srvGpu, err)
        && makeHeap(d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256, true, sampGpu, err);
}

// A depth texture that is also sampled is created typeless (R32), viewed as D32 for depth and
// R32_FLOAT for reads; any other texture uses its format directly.
TextureHandle D3d12Device::createTexture(const TextureDesc& desc, std::string& err){
    if (desc.width == 0 || desc.height == 0){ err = "d3d12: textures are not empty"; return {}; }
    const bool depth = isDepth(desc.format), sampled = has(desc.usage, TextureUsage::Sampled);
    if (sampled && desc.format != TextureFormat::RGBA8Unorm && desc.format != TextureFormat::RGBA16Float && !depth){
        err = "d3d12: only RGBA8Unorm, RGBA16Float and depth textures are sampled"; return {}; }
    if (!srvCpu.heap && !initHeaps(err)) return {};
    TexRec t;
    t.w = desc.width; t.h = desc.height; t.usage = desc.usage; t.format = desc.format;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc.width; rd.Height = desc.height; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = depth && sampled ? DXGI_FORMAT_R32_TYPELESS : dxgiFormat(desc.format);
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const bool target = has(desc.usage, TextureUsage::RenderTarget);
    rd.Flags = !target ? D3D12_RESOURCE_FLAG_NONE : depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t.res));
    if (FAILED(hr)){ err = hrError("CreateCommittedResource (texture)", hr); return {}; }
    if (desc.label && *desc.label){ std::wstring w(desc.label, desc.label + std::strlen(desc.label)); t.res->SetName(w.c_str()); }
    if (sampled){
        if (srvCpu.next >= srvCpu.capacity){ err = "d3d12: out of SRV slots"; return {}; }
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = depth ? DXGI_FORMAT_R32_FLOAT : dxgiFormat(desc.format); sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
        t.srv = srvCpu.next++;
        device_->CreateShaderResourceView(t.res.Get(), &sv, cpuAt(srvCpu, t.srv));
    }
    if (target && depth){
        if (dsvCpu.next >= dsvCpu.capacity){ err = "d3d12: out of DSV slots"; return {}; }
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        t.dsv = dsvCpu.next++;
        device_->CreateDepthStencilView(t.res.Get(), &dv, cpuAt(dsvCpu, t.dsv));
    } else if (target){
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

void D3d12CommandList::transition(TexRec& t, D3D12_RESOURCE_STATES to){
    if (t.state == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res.Get(); b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t.state; b.Transition.StateAfter = to;
    dev_.list()->ResourceBarrier(1, &b);
    t.state = to;
}
void D3d12CommandList::uploadTexture(TextureHandle dst, const void* data, uint32_t width, uint32_t height){
    TexRec* t = dev_.texture(dst);
    if (!t){ fail("d3d12: upload to an unknown texture"); return; }
    if (width != t->w || height != t->h){ fail("d3d12: texture upload size differs from the texture"); return; }
    if (isDepth(t->format)){ fail("d3d12: depth textures take no uploads"); return; }
    D3D12_RESOURCE_DESC rd = t->res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 total = 0;
    dev_.d3d()->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &total);
    ComPtr<ID3D12Resource> s; std::string err;
    if (!dev_.committed(total, D3D12_HEAP_TYPE_UPLOAD, s, err)){ fail(err); return; }
    staging.push_back(s);
    unsigned char* p = nullptr; D3D12_RANGE none{0, 0};
    HRESULT hr = s->Map(0, &none, (void**)&p);
    if (FAILED(hr)){ fail(hrError("texture upload Map", hr)); return; }
    const size_t row = size_t(width) * bytesPerTexel(t->format);
    for (uint32_t y = 0; y < height; ++y) std::memcpy(p + fp.Offset + size_t(y) * fp.Footprint.RowPitch, (const unsigned char*)data + size_t(y) * row, row);
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
    const uint32_t pitch = textureRowPitch(t->w, t->format);
    if (b->size < uint64_t(pitch) * t->h){ fail("d3d12: texture copy into a buffer that is too small"); return; }
    transition(*t, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION d{}, s{};
    s.pResource = t->res.Get(); s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    d.pResource = b->res.Get(); d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    d.PlacedFootprint.Offset = 0;
    d.PlacedFootprint.Footprint = {t->res->GetDesc().Format, t->w, t->h, 1, pitch};
    dev_.list()->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
}
}  // namespace raw::rhi::d3d12
