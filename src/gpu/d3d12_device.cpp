#include "d3d12_device.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace raw::d3d12 {
using Microsoft::WRL::ComPtr;
namespace {
struct State { bool tried{false}, ok{false}; GpuAdapterInfo info; std::string error; };
State& state(){ static State s; return s; }
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
bool pickAdapter(Device& dev, std::string& err){
    ComPtr<IDXGIFactory6> f;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&f));
    if (FAILED(hr)){ err = hrError("CreateDXGIFactory2", hr); return false; }
    if (envOn("RAW_NATIVE_D3D12_WARP")){
        if (FAILED(hr = f->EnumWarpAdapter(IID_PPV_ARGS(&dev.adapter)))){ err = hrError("EnumWarpAdapter", hr); return false; }
        if (!(dev.device = tryDevice(dev.adapter.Get()))){ err = "WARP did not create a D3D12 device with shader model 6.0"; return false; }
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
        if ((dev.device = tryDevice(a.Get()))){ dev.adapter = a; return true; }
        skipped += (skipped.empty() ? "" : ", ") + utf8(d.Description) + " (no D3D12 device with shader model 6.0)";
    }
    err = "no hardware D3D12 adapter" + (skipped.empty() ? std::string() : "; skipped: " + skipped);
    return false;
}
}  // namespace
std::string hrError(const std::string& what, HRESULT hr){
    char b[32]; std::snprintf(b, sizeof b, "0x%08lX", (unsigned long)hr);
    return what + " failed: HRESULT " + b;
}
Device& Device::get(){ static Device d; return d; }
bool Device::init(GpuAdapterInfo& info, std::string& err){
    State& s = state();
    if (s.tried){ info = s.info; err = s.error; return s.ok; }
    s.tried = true;
    Device& dev = get();
    if (envOn("RAW_NATIVE_D3D12_DEBUG")){
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
    }
    if (!pickAdapter(dev, s.error)){ err = s.error; return false; }
    DXGI_ADAPTER_DESC1 d{};
    dev.adapter->GetDesc1(&d);
    char id[16]; std::snprintf(id, sizeof id, "0x%04X", (unsigned)d.DeviceId);
    s.info = GpuAdapterInfo{vendorName(d.VendorId), "", id, utf8(d.Description), "d3d12", driverVersion(dev.adapter.Get())};
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT hr;
    if (FAILED(hr = dev.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&dev.queue)))) s.error = hrError("CreateCommandQueue", hr);
    else if (FAILED(hr = dev.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&dev.alloc)))) s.error = hrError("CreateCommandAllocator", hr);
    else if (FAILED(hr = dev.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dev.alloc.Get(), nullptr, IID_PPV_ARGS(&dev.list)))) s.error = hrError("CreateCommandList", hr);
    else if (FAILED(hr = dev.list->Close())) s.error = hrError("CommandList Close", hr);
    else if (FAILED(hr = dev.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dev.fence)))) s.error = hrError("CreateFence", hr);
    else if (!(dev.event = CreateEventW(nullptr, FALSE, FALSE, nullptr))) s.error = "CreateEvent failed";
    s.ok = s.error.empty();
    info = s.info; err = s.error;
    return s.ok;
}
bool Device::buffer(uint64_t size, D3D12_HEAP_TYPE heap, ComPtr<ID3D12Resource>& out, std::string& err){
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size < 16 ? 16 : (size + 15) & ~uint64_t(15);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = heap == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const D3D12_RESOURCE_STATES st = heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                   : heap == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, IID_PPV_ARGS(&out));
    if (FAILED(hr)){ err = hrError("CreateCommittedResource (" + std::to_string(size) + " bytes)", hr); return false; }
    return true;
}
bool Device::write(ID3D12Resource* upload, uint64_t offset, const void* data, uint64_t size, std::string& err){
    unsigned char* p = nullptr;
    D3D12_RANGE none{0, 0};
    HRESULT hr = upload->Map(0, &none, (void**)&p);
    if (FAILED(hr)){ err = hrError("upload Map", hr); return false; }
    std::memcpy(p + offset, data, (size_t)size);
    upload->Unmap(0, nullptr);
    return true;
}
ID3D12GraphicsCommandList* Device::begin(std::string& err){
    HRESULT hr = alloc->Reset();
    if (SUCCEEDED(hr)) hr = list->Reset(alloc.Get(), nullptr);
    if (FAILED(hr)){ err = hrError("command list Reset", hr); return nullptr; }
    return list.Get();
}
bool Device::submitAndWait(std::string& err){
    HRESULT hr = list->Close();
    if (FAILED(hr)){ err = hrError("command list Close", hr); return false; }
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    const uint64_t v = ++fenceValue;
    if (FAILED(hr = queue->Signal(fence.Get(), v))){ err = hrError("queue Signal", hr); return false; }
    if (fence->GetCompletedValue() < v){
        if (FAILED(hr = fence->SetEventOnCompletion(v, event))){ err = hrError("SetEventOnCompletion", hr); return false; }
        WaitForSingleObject(event, INFINITE);
    }
    if (FAILED(hr = device->GetDeviceRemovedReason())){ err = hrError("the D3D12 device was removed", hr); return false; }
    return true;
}
}
