#pragma once
// The render hardware interface: the one boundary between the renderer and a
// graphics API. Backends implement it in src/rhi/<backend>/ and are the only
// code that includes an API header (d3d12.h, webgpu.h). A build links exactly
// one backend today: d3d12, webgpu, or null (no GPU).
//
// The object model follows WebGPU and wgpu-hal: a Device creates buffers and
// pipelines, a CommandList records copies and dispatches, and the device
// submits one list at a time. The RHI does no hazard tracking of its own. The
// frame graph (raw/graph/frame_graph.hpp) declares each pass's accesses and
// hands the RHI explicit barriers; a backend maps them to its API and drops
// the ones its API does not need (WebGPU synchronizes passes itself).
//
// Version 1 covered buffers, compute pipelines, uploads, copies, dispatches and
// read-back. Version 2 adds RGBA8 textures, samplers, raster pipelines and
// render passes (ROADMAP M2 criterion 3). Version 3 adds float and depth formats, up to four
// colour targets and a depth target per pass, depth testing and culling, and read-only
// storage buffers in raster stages for vertex pulling (ROADMAP M3; evidence/m3-rhi3-bounds.json).
// Swapchains, queries and multiple
// queues are added behind the same handles when the renderer needs them
// (docs/architecture/adr/0002-rhi.md).
//
// Textures track their own state inside the backend: an upload, a draw that
// samples it, a render pass that targets it and a copy out of it each move it to
// the state that use needs. Buffers still take explicit barriers.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace raw::rhi {
// Bumped when a backend or a caller must change to keep working.
inline constexpr int kRhiVersion = 3;

// What the adapter says about itself. Strings come from the API and are
// recorded verbatim in certificates; empty means the API did not say.
struct AdapterInfo {
    std::string vendor, architecture, device, description, backend, driver;
};

// A generational handle: index into the backend's slot table plus a
// generation, so a stale handle is detected instead of aliasing a new object.
// Generation 0 is never issued, so a default handle is invalid.
template<class Tag> struct Handle {
    uint32_t index{0}, gen{0};
    bool valid() const { return gen != 0; }
    friend bool operator==(Handle, Handle) = default;
};
using BufferHandle = Handle<struct BufferTag>;
using PipelineHandle = Handle<struct PipelineTag>;
using TextureHandle = Handle<struct TextureTag>;
using SamplerHandle = Handle<struct SamplerTag>;
using RasterPipelineHandle = Handle<struct RasterPipelineTag>;

enum class BufferUsage : uint32_t {
    None = 0, Uniform = 1u << 0, Storage = 1u << 1, CopySrc = 1u << 2, CopyDst = 1u << 3, MapRead = 1u << 4,
};
constexpr BufferUsage operator|(BufferUsage a, BufferUsage b){ return BufferUsage((uint32_t)a | (uint32_t)b); }
constexpr bool has(BufferUsage set, BufferUsage bit){ return ((uint32_t)set & (uint32_t)bit) != 0; }

struct BufferDesc {
    uint64_t size{0};
    BufferUsage usage{BufferUsage::None};
    const char* label{""};
};

// How a pass touches a buffer. StorageWrite is read-write storage access.
// Undefined is the state of a buffer no pass has touched yet.
enum class Access : uint8_t { Undefined, Uniform, StorageRead, StorageWrite, CopySrc, CopyDst };
constexpr bool isWrite(Access a){ return a == Access::StorageWrite || a == Access::CopyDst; }

// One barrier between two accesses of a buffer. Same-state pairs that involve
// a write are hazards the backend must still order (a UAV barrier on D3D12).
struct BufferBarrier {
    BufferHandle buffer;
    Access before{Access::Undefined}, after{Access::Undefined};
};

// The binding layout of a compute shader, binding i of group 0 in order. It is
// generated from the WGSL source (src/renderer/gpu/shaders/pass_layout.hpp).
enum class Binding : uint8_t { Uniform, StorageRead, StorageReadWrite };

enum class ShaderFormat : uint8_t { Wgsl, Dxil };
struct ShaderCode {
    ShaderFormat format{ShaderFormat::Wgsl};
    const void* bytes{nullptr};   // WGSL text (not null-terminated) or DXIL bytecode
    std::size_t size{0};
};
struct ComputePipelineDesc {
    const char* label{""};
    ShaderCode code;
    std::span<const Binding> layout;
};

// Textures: 2D, one mip level. RGBA8Unorm is linear (no sRGB decode on sampling). The float
// formats and Depth32Float are render targets read back by copy; RGBA8Unorm and RGBA16Float
// can also be sampled.
enum class TextureFormat : uint8_t { RGBA8Unorm, RGBA16Float, RGBA32Float, R32Float, Depth32Float };
constexpr uint32_t bytesPerTexel(TextureFormat f){
    switch (f){
        case TextureFormat::RGBA16Float: return 8;
        case TextureFormat::RGBA32Float: return 16;
        default: return 4;
    }
}
constexpr bool isDepth(TextureFormat f){ return f == TextureFormat::Depth32Float; }
enum class TextureUsage : uint32_t { None = 0, Sampled = 1u << 0, RenderTarget = 1u << 1, CopyDst = 1u << 2, CopySrc = 1u << 3 };
constexpr TextureUsage operator|(TextureUsage a, TextureUsage b){ return TextureUsage((uint32_t)a | (uint32_t)b); }
constexpr bool has(TextureUsage set, TextureUsage bit){ return ((uint32_t)set & (uint32_t)bit) != 0; }
struct TextureDesc {
    uint32_t width{0}, height{0};
    TextureFormat format{TextureFormat::RGBA8Unorm};
    TextureUsage usage{TextureUsage::None};
    const char* label{""};
};
// Rows of a texture copied into a buffer are this many bytes apart (D3D12 and
// WebGPU both require 256-byte row alignment for texture-buffer copies).
constexpr uint32_t textureRowPitch(uint32_t width, TextureFormat f = TextureFormat::RGBA8Unorm){
    return (width * bytesPerTexel(f) + 255) & ~255u;
}

enum class Filter : uint8_t { Nearest, Linear };
enum class AddressMode : uint8_t { ClampToEdge, Repeat };
struct SamplerDesc {
    Filter filter{Filter::Linear};
    AddressMode address{AddressMode::ClampToEdge};
    const char* label{""};
};

// A raster pipeline draws triangles from vertex indices alone (no vertex buffers: a
// vertex shader pulls what it needs from read-only storage buffers), into up to four
// colour targets and an optional Depth32Float target, with no blending. Front faces are
// counter-clockwise. Its bindings are group 0 in order, as the WGSL declares them; the
// layout is generated from the WGSL (src/renderer/gpu/shaders/raster_layout.hpp).
enum class RasterBinding : uint8_t { Uniform, Texture, Sampler, StorageRead };
enum class CompareFunc : uint8_t { Less, LessEqual, Equal, Always };
enum class CullMode : uint8_t { None, Back, Front };
struct DepthState { bool enabled{false}, write{true}; CompareFunc compare{CompareFunc::Less}; };
inline constexpr uint32_t kMaxColorTargets = 4;
struct RasterPipelineDesc {
    const char* label{""};
    ShaderCode vertex, fragment;   // WGSL: one module with entry points vs and fs, given twice
    std::span<const RasterBinding> layout;
    TextureFormat targets[kMaxColorTargets]{TextureFormat::RGBA8Unorm, TextureFormat::RGBA8Unorm,
                                            TextureFormat::RGBA8Unorm, TextureFormat::RGBA8Unorm};
    uint32_t targetCount{1};
    DepthState depth;               // enabled: the pass has a Depth32Float target
    CullMode cull{CullMode::None};
};
// One binding of a draw: the field that matches its layout slot is used (a StorageRead slot
// takes a buffer, which the caller has moved to Access::StorageRead with a barrier).
struct RasterBind { BufferHandle buffer; TextureHandle texture; SamplerHandle sampler; };
struct ColorAttachment { TextureHandle texture; float clear[4]{0, 0, 0, 0}; };
struct RenderPassDesc {
    ColorAttachment color[kMaxColorTargets];
    uint32_t colorCount{1};
    TextureHandle depth;            // a Depth32Float target, or invalid for none
    float depthClear{1.0f};
};

// Records one submission. Errors are kept and reported by submitAndWait, so a
// recording call never needs a return value. Uploads are ordered before the
// list's other commands that follow them.
class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void barrier(std::span<const BufferBarrier> barriers) = 0;
    virtual void upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size) = 0;
    virtual void copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset,
                            uint64_t size) = 0;
    virtual void dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds,
                          uint32_t x, uint32_t y, uint32_t z) = 0;
    // The whole texture from tight rows of bytesPerTexel(format) texels, top row first.
    virtual void uploadTexture(TextureHandle dst, const void* rgba, uint32_t width, uint32_t height) = 0;
    // The whole texture into a buffer at offset 0, rows textureRowPitch(width, format) apart.
    virtual void copyTextureToBuffer(TextureHandle src, BufferHandle dst) = 0;
    // A render pass clears its target; draws go between begin and end.
    virtual void beginRenderPass(const RenderPassDesc& pass) = 0;
    virtual void draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount) = 0;
    virtual void endRenderPass() = 0;
};

class Device {
public:
    virtual ~Device() = default;
    virtual const char* backendName() const = 0;
    virtual const AdapterInfo& adapter() const = 0;
    virtual ShaderFormat shaderFormat() const = 0;
    // Buffers are zero-filled at creation on every backend.
    virtual BufferHandle createBuffer(const BufferDesc& desc, std::string& err) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;
    virtual PipelineHandle createComputePipeline(const ComputePipelineDesc& desc, std::string& err) = 0;
    virtual TextureHandle createTexture(const TextureDesc& desc, std::string& err) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    virtual SamplerHandle createSampler(const SamplerDesc& desc, std::string& err) = 0;
    virtual RasterPipelineHandle createRasterPipeline(const RasterPipelineDesc& desc, std::string& err) = 0;
    // Open the command list for the next submission. One list is open at a time.
    virtual CommandList* begin(std::string& err) = 0;
    // Execute the open list and block until the GPU finishes it.
    virtual bool submitAndWait(std::string& err) = 0;
    // Map a MapRead buffer after the submission that wrote it. Valid until unmap.
    virtual const void* mapRead(BufferHandle buffer, uint64_t size, std::string& err) = 0;
    virtual void unmap(BufferHandle buffer) = 0;
};

// The backend linked into this build: "d3d12", "webgpu" or "none".
const char* linkedBackend();
// The device of the linked backend, created on first call and kept for the
// process. Null with the reason when there is no backend or no adapter.
Device* device(std::string& err);
}
