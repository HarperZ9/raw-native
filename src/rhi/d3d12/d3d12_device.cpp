// The D3D12 device: adapter choice, buffers, pipelines and submission.
#include "d3d12_device.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace raw::rhi::d3d12 {
namespace {
bool envOn(const char* name){ const char* v = std::getenv(name); return v && std::strcmp(v, "1") == 0; }
std::string utf8(const wchar_t* w){
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
const char* vendorName(UINT id){
    switch (id){ case 0x10DE: return "nvidia"; case 0x1002: case 0x1022: return "amd";
        case 0x8086: return "intel"; case 0x1414: return "microsoft"; case 0x5143: return "qualcomm"; default: return ""; }
}
// The user-mode driver version, as DXGI reports it: a.b.c.d.
std::string driverVersion(IDXGIAdapter1* a){
    LARGE_INTEGER v{};
    if (FAILED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v))) return {};
    char b[48];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", (unsigned)((unsigned long)v.HighPart >> 16), (unsigned)(v.HighPart & 0xFFFF),
                  (unsigned)(v.LowPart >> 16), (unsigned)(v.LowPart & 0xFFFF));
    return b;
}
// A device on `a` that supports compute shader model 6.0, or null.
ComPtr<ID3D12Device> tryDevice(IDXGIAdapter1* a){
    ComPtr<ID3D12Device> d;
    if (FAILED(D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d)))) return nullptr;
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_0};
    if (FAILED(d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) || sm.HighestShaderModel < D3D_SHADER_MODEL_6_0) return nullptr;
    return d;
}
// Name an object for PIX, RenderDoc and debug-layer messages.
void name(ID3D12Object* o, const char* label){
    if (!label || !*label) return;
    std::wstring w(label, label + std::strlen(label));
    o->SetName(w.c_str());
}
}  // namespace
std::string hrError(const std::string& what, HRESULT hr){
    char b[32]; std::snprintf(b, sizeof b, "0x%08lX", (unsigned long)hr);
    return what + " failed: HRESULT " + b;
}
bool D3d12Device::pickAdapter(std::string& err){
    ComPtr<IDXGIFactory6> f;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&f));
    if (FAILED(hr)){ err = hrError("CreateDXGIFactory2", hr); return false; }
    if (envOn("RAW_NATIVE_D3D12_WARP")){
        if (FAILED(hr = f->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)))){ err = hrError("EnumWarpAdapter", hr); return false; }
        if (!(device_ = tryDevice(adapter_.Get()))){ err = "WARP did not create a D3D12 device with shader model 6.0"; return false; }
        return true;
    }
    std::string skipped;
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; f->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) != DXGI_ERROR_NOT_FOUND; ++i){
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        // The Basic Render Driver is software; on a machine without a GPU (a CI
        // runner) DXGI can list it without the software flag, so the vendor id
        // decides too. Microsoft (0x1414) ships no hardware D3D12 adapter.
        if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || d.VendorId == 0x1414){
            skipped += (skipped.empty() ? "" : ", ") + utf8(d.Description) + " (software)"; continue; }
        if ((device_ = tryDevice(a.Get()))){ adapter_ = a; return true; }
        skipped += (skipped.empty() ? "" : ", ") + utf8(d.Description) + " (no D3D12 device with shader model 6.0)";
    }
    err = "no hardware D3D12 adapter" + (skipped.empty() ? std::string() : "; skipped: " + skipped);
    return false;
}
bool D3d12Device::init(std::string& err){
    if (envOn("RAW_NATIVE_D3D12_DEBUG")){
        ComPtr<ID3D12Debug> dbg;
        if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))){
            err = "RAW_NATIVE_D3D12_DEBUG=1 but the D3D12 debug layer is not installed (Windows Graphics Tools)";
            return false;
        }
        dbg->EnableDebugLayer();
    }
    if (!pickAdapter(err)) return false;
    if (envOn("RAW_NATIVE_D3D12_DEBUG")) device_.As(&infoQueue_);
    DXGI_ADAPTER_DESC1 d{};
    adapter_->GetDesc1(&d);
    char id[16]; std::snprintf(id, sizeof id, "0x%04X", (unsigned)d.DeviceId);
    info_ = AdapterInfo{vendorName(d.VendorId), "", id, utf8(d.Description), "d3d12", driverVersion(adapter_.Get())};
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT hr;
    if (FAILED(hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)))) err = hrError("CreateCommandQueue", hr);
    else if (FAILED(hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_)))) err = hrError("CreateCommandAllocator", hr);
    else if (FAILED(hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_.Get(), nullptr, IID_PPV_ARGS(&list_)))) err = hrError("CreateCommandList", hr);
    else if (FAILED(hr = list_->Close())) err = hrError("CommandList Close", hr);
    else if (FAILED(hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) err = hrError("CreateFence", hr);
    else if (!(event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr))) err = "CreateEvent failed";
    return err.empty();
}
bool D3d12Device::committed(uint64_t size, D3D12_HEAP_TYPE heap, ComPtr<ID3D12Resource>& out, std::string& err){
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size < 16 ? 16 : (size + 15) & ~uint64_t(15);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = heap == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const D3D12_RESOURCE_STATES st = heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                   : heap == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    HRESULT hr = device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, IID_PPV_ARGS(&out));
    if (FAILED(hr)){ err = hrError("CreateCommittedResource (" + std::to_string(size) + " bytes)", hr); return false; }
    return true;
}
BufferHandle D3d12Device::createBuffer(const BufferDesc& desc, std::string& err){
    BufferRec r;
    r.size = desc.size;
    r.heap = has(desc.usage, BufferUsage::MapRead) ? D3D12_HEAP_TYPE_READBACK
           : has(desc.usage, BufferUsage::Uniform) ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
    // Root CBVs need 256-byte aligned addresses; a committed buffer starts aligned
    // and is sized to a multiple of 256 here.
    const uint64_t size = r.heap == D3D12_HEAP_TYPE_UPLOAD ? (desc.size + 255) & ~uint64_t(255) : desc.size;
    if (!committed(size, r.heap, r.res, err)) return {};
    name(r.res.Get(), desc.label);
    auto id = buffers_.insert(std::move(r));
    return {id.index, id.gen};
}
void D3d12Device::destroyBuffer(BufferHandle h){ buffers_.erase(h.index, h.gen); }

PipelineHandle D3d12Device::createComputePipeline(const ComputePipelineDesc& desc, std::string& err){
    const std::string pass = desc.label;
    if (desc.code.format != ShaderFormat::Dxil || !desc.code.bytes){ err = "pass " + pass + ": the D3D12 backend takes DXIL"; return {}; }
    if (desc.layout.size() > 8){ err = "pass " + pass + ": more than 8 bindings"; return {}; }
    D3D12_ROOT_PARAMETER params[8] = {};
    UINT uniforms = 0;
    for (size_t i = 0; i < desc.layout.size(); ++i){
        const bool cbv = desc.layout[i] == Binding::Uniform;
        params[i].ParameterType = cbv ? D3D12_ROOT_PARAMETER_TYPE_CBV : D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[i].Descriptor.ShaderRegister = cbv ? uniforms++ : (UINT)i;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rd = {};
    rd.NumParameters = (UINT)desc.layout.size(); rd.pParameters = params;
    ComPtr<ID3DBlob> blob, msg;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &msg);
    if (FAILED(hr)){ err = hrError("root signature for pass " + pass, hr) + (msg ? ": " + std::string((const char*)msg->GetBufferPointer(), msg->GetBufferSize()) : ""); return {}; }
    PipeRec p;
    p.layout.assign(desc.layout.begin(), desc.layout.end());
    if (FAILED(hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&p.rs)))){
        err = hrError("CreateRootSignature " + pass, hr); return {}; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = p.rs.Get();
    pd.CS = {desc.code.bytes, desc.code.size};
    if (FAILED(hr = device_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&p.pso)))){
        err = hrError("CreateComputePipelineState " + pass, hr); return {}; }
    name(p.pso.Get(), desc.label);
    auto id = pipes_.insert(std::move(p));
    return {id.index, id.gen};
}
CommandList* D3d12Device::begin(std::string& err){
    if (open_){ err = "d3d12: a command list is already open"; return nullptr; }
    HRESULT hr = alloc_->Reset();
    if (SUCCEEDED(hr)) hr = list_->Reset(alloc_.Get(), nullptr);
    if (FAILED(hr)){ err = hrError("command list Reset", hr); return nullptr; }
    cmd_.error.clear();
    cmd_.target = {};
    srvGpu.next = 0; sampGpu.next = 0;   // the last submission finished (submitAndWait blocks)
    open_ = true;
    return &cmd_;
}
bool D3d12Device::submitAndWait(std::string& err){
    if (!open_){ err = "d3d12: no command list is open"; return false; }
    open_ = false;
    HRESULT hr = list_->Close();
    if (!cmd_.error.empty()){ err = cmd_.error; cmd_.staging.clear(); return false; }
    if (FAILED(hr)){ err = hrError("command list Close", hr); cmd_.staging.clear(); return false; }
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const uint64_t v = ++fenceValue_;
    bool ok = true;
    if (FAILED(hr = queue_->Signal(fence_.Get(), v))){ err = hrError("queue Signal", hr); ok = false; }
    else if (fence_->GetCompletedValue() < v){
        if (FAILED(hr = fence_->SetEventOnCompletion(v, event_))){ err = hrError("SetEventOnCompletion", hr); ok = false; }
        else WaitForSingleObject(event_, INFINITE);
    }
    if (ok && FAILED(hr = device_->GetDeviceRemovedReason())){ err = hrError("the D3D12 device was removed", hr); ok = false; }
    if (ok && !debugErrors(err)) ok = false;
    if (ok) cmd_.staging.clear();   // on failure the GPU may still read them; keep them alive
    return ok;
}
// With the debug layer on, any error or corruption message it stored fails the
// submission, so a wrong barrier is a failed run and never a lucky pass.
bool D3d12Device::debugErrors(std::string& err){
    if (!infoQueue_) return true;
    std::string first;
    UINT64 errors = 0;
    for (UINT64 i = 0, n = infoQueue_->GetNumStoredMessages(); i < n; ++i){
        SIZE_T len = 0;
        if (FAILED(infoQueue_->GetMessage(i, nullptr, &len))) continue;
        std::vector<char> buf(len);
        auto* m = (D3D12_MESSAGE*)buf.data();
        if (FAILED(infoQueue_->GetMessage(i, m, &len))) continue;
        if (m->Severity != D3D12_MESSAGE_SEVERITY_ERROR && m->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION) continue;
        if (errors++ == 0) first.assign(m->pDescription, m->DescriptionByteLength ? m->DescriptionByteLength - 1 : 0);
    }
    infoQueue_->ClearStoredMessages();
    if (errors == 0) return true;
    err = "the D3D12 debug layer reported " + std::to_string(errors) + " error(s); first: " + first;
    return false;
}
const void* D3d12Device::mapRead(BufferHandle h, uint64_t size, std::string& err){
    BufferRec* r = buffer(h);
    if (!r || r->heap != D3D12_HEAP_TYPE_READBACK){ err = "d3d12: mapRead needs a MapRead buffer"; return nullptr; }
    void* p = nullptr;
    D3D12_RANGE range{0, (SIZE_T)size};
    HRESULT hr = r->res->Map(0, &range, &p);
    if (FAILED(hr)){ err = hrError("readback Map", hr); return nullptr; }
    return p;
}
void D3d12Device::unmap(BufferHandle h){
    if (BufferRec* r = buffer(h)){ D3D12_RANGE none{0, 0}; r->res->Unmap(0, &none); }
}
}  // namespace raw::rhi::d3d12

namespace raw::rhi {
const char* linkedBackend(){ return "d3d12"; }
Device* device(std::string& err){
    struct Once { std::unique_ptr<d3d12::D3d12Device> dev; std::string error; bool tried{false}; };
    static Once once;
    if (!once.tried){
        once.tried = true;
        auto d = std::make_unique<d3d12::D3d12Device>();
        if (d->init(once.error)) once.dev = std::move(d);
    }
    if (!once.dev) err = once.error;
    return once.dev.get();
}
}
