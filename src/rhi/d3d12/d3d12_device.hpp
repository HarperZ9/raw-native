#pragma once
// The D3D12 backend of the RHI. Private to src/rhi/d3d12/: nothing outside
// this directory includes a D3D12 or DXGI header. Windows SDK only
// (d3d12.dll, dxgi.dll); shaders arrive as DXIL from the renderer.
//
// Memory: each buffer is a committed resource. Uniform buffers live in an
// UPLOAD heap and are written by the CPU directly; MapRead buffers live in a
// READBACK heap; every other buffer lives in a DEFAULT heap with unordered
// access allowed. Only DEFAULT-heap buffers take barriers.
// Binding: binding i of a pipeline is root parameter i, a root CBV for a
// uniform and a root UAV (register u<i>) for every storage buffer, so no
// descriptor heaps are needed by compute.
// Textures and raster pipelines (d3d12_raster.cpp): a texture is a committed RGBA8
// resource that tracks its own state; its SRV and RTV live in CPU-only heaps and are
// copied into shader-visible rings at each draw. A raster pipeline's uniform is a root
// CBV, each texture an SRV table and each sampler a sampler table.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "raw/core/slot_pool.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rhi::d3d12 {
using Microsoft::WRL::ComPtr;
std::string hrError(const std::string& what, HRESULT hr);

struct BufferRec { ComPtr<ID3D12Resource> res; D3D12_HEAP_TYPE heap{D3D12_HEAP_TYPE_DEFAULT}; uint64_t size{0}; };
struct PipeRec { ComPtr<ID3D12RootSignature> rs; ComPtr<ID3D12PipelineState> pso; std::vector<Binding> layout; };
struct TexRec {
    ComPtr<ID3D12Resource> res; uint32_t w{0}, h{0}; TextureUsage usage{TextureUsage::None};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON}; UINT srv{~0u}, rtv{~0u};
};
struct SampRec { UINT slot{0}; };
struct RasterRec { ComPtr<ID3D12RootSignature> rs; ComPtr<ID3D12PipelineState> pso; std::vector<RasterBinding> layout; };
// A descriptor heap and the next free slot in it.
struct Heap { ComPtr<ID3D12DescriptorHeap> heap; UINT size{0}, next{0}, capacity{0}; };

class D3d12Device;
class D3d12CommandList final : public CommandList {
public:
    explicit D3d12CommandList(D3d12Device& dev) : dev_(dev) {}
    void barrier(std::span<const BufferBarrier> barriers) override;
    void upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size) override;
    void copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z) override;
    void uploadTexture(TextureHandle dst, const void* rgba, uint32_t width, uint32_t height) override;
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst) override;
    void beginRenderPass(const RenderPassDesc& pass) override;
    void draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount) override;
    void endRenderPass() override;
    // Move a texture to `to`, recording the transition when it is not there already.
    void transition(TexRec& t, D3D12_RESOURCE_STATES to);
    TextureHandle target;   // the open render pass's target
    // The first recording error, reported by submitAndWait.
    std::string error;
    // Staging buffers for uploads into DEFAULT-heap buffers, released after the
    // submission completes.
    std::vector<ComPtr<ID3D12Resource>> staging;
private:
    void fail(const std::string& e){ if (error.empty()) error = e; }
    D3d12Device& dev_;
};

class D3d12Device final : public Device {
public:
    // Pick the first hardware adapter in the DXGI high-performance order that
    // creates a feature level 11_0 device with shader model 6.0. Software
    // adapters (WARP, the Basic Render Driver: the software flag or Microsoft's
    // vendor id 0x1414) are skipped unless the environment sets
    // RAW_NATIVE_D3D12_WARP=1, which uses WARP explicitly.
    // RAW_NATIVE_D3D12_DEBUG=1 enables the D3D12 debug layer, and any error it
    // reports fails the submission (init fails when the layer is missing).
    bool init(std::string& err);

    const char* backendName() const override { return "d3d12"; }
    const AdapterInfo& adapter() const override { return info_; }
    ShaderFormat shaderFormat() const override { return ShaderFormat::Dxil; }
    BufferHandle createBuffer(const BufferDesc& desc, std::string& err) override;
    void destroyBuffer(BufferHandle buffer) override;
    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc, std::string& err) override;
    TextureHandle createTexture(const TextureDesc& desc, std::string& err) override;
    void destroyTexture(TextureHandle texture) override;
    SamplerHandle createSampler(const SamplerDesc& desc, std::string& err) override;
    RasterPipelineHandle createRasterPipeline(const RasterPipelineDesc& desc, std::string& err) override;
    CommandList* begin(std::string& err) override;
    bool submitAndWait(std::string& err) override;
    const void* mapRead(BufferHandle buffer, uint64_t size, std::string& err) override;
    void unmap(BufferHandle buffer) override;

    BufferRec* buffer(BufferHandle h){ return buffers_.get(h.index, h.gen); }
    PipeRec* pipeline(PipelineHandle h){ return pipes_.get(h.index, h.gen); }
    TexRec* texture(TextureHandle h){ return textures_.get(h.index, h.gen); }
    SampRec* sampler(SamplerHandle h){ return samplers_.get(h.index, h.gen); }
    RasterRec* raster(RasterPipelineHandle h){ return rasters_.get(h.index, h.gen); }
    // CPU-only heaps hold each texture's SRV and RTV and each sampler; the draw copies
    // them into the shader-visible rings, which rewind at begin().
    Heap srvCpu, sampCpu, rtvCpu, srvGpu, sampGpu;
    bool initHeaps(std::string& err);
    ID3D12Device* d3d(){ return device_.Get(); }
    ID3D12GraphicsCommandList* list(){ return list_.Get(); }
    // A committed buffer in `heap`, in the state that heap requires.
    bool committed(uint64_t size, D3D12_HEAP_TYPE heap, ComPtr<ID3D12Resource>& out, std::string& err);

private:
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_{};
    uint64_t fenceValue_{0};
    AdapterInfo info_;
    SlotPool<BufferRec> buffers_;
    SlotPool<PipeRec> pipes_;
    SlotPool<TexRec> textures_;
    SlotPool<SampRec> samplers_;
    SlotPool<RasterRec> rasters_;
    D3d12CommandList cmd_{*this};
    bool open_{false};
    ComPtr<ID3D12InfoQueue> infoQueue_;   // set with RAW_NATIVE_D3D12_DEBUG=1
    bool pickAdapter(std::string& err);
    bool debugErrors(std::string& err);
};
}
