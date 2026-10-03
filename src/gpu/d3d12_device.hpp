#pragma once
// The D3D12 device, queue and command list the backend renders with, created
// once per process. Windows SDK headers only.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "raw/gpu_reconcile.hpp"
#include <cstdint>
#include <string>
namespace raw::d3d12 {
// The Params constant buffer occupies the first 256 bytes of the upload buffer
// (root CBVs need 256-byte alignment); the triangles follow it.
inline constexpr uint64_t kParamsBytes = 256;
std::string hrError(const std::string& what, HRESULT hr);
struct Device {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE event{};
    uint64_t fenceValue{0};
    static Device& get();
    // Pick the first hardware adapter in the DXGI high-performance order that
    // creates a feature level 11_0 device with shader model 6.0, once. Software
    // adapters (WARP, the Basic Render Driver: the software flag or Microsoft's
    // vendor id 0x1414) are skipped unless the environment
    // sets RAW_NATIVE_D3D12_WARP=1, which uses WARP explicitly.
    // RAW_NATIVE_D3D12_DEBUG=1 enables the D3D12 debug layer.
    static bool init(GpuAdapterInfo& info, std::string& err);
    // A committed buffer; DEFAULT buffers allow unordered access and start in COMMON.
    bool buffer(uint64_t size, D3D12_HEAP_TYPE heap, Microsoft::WRL::ComPtr<ID3D12Resource>& out, std::string& err);
    // Copy bytes into an UPLOAD buffer.
    bool write(ID3D12Resource* upload, uint64_t offset, const void* data, uint64_t size, std::string& err);
    // Reset the allocator and open the command list.
    ID3D12GraphicsCommandList* begin(std::string& err);
    // Close, execute, and block until the GPU finishes. Fails on device removal.
    bool submitAndWait(std::string& err);
};
}
