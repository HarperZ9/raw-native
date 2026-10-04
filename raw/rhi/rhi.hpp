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
// This first version covers what the renderer uses: buffers, compute
// pipelines, uploads, copies, dispatches and read-back. Textures, samplers,
// graphics pipelines, swapchains, queries and multiple queues are added
// behind the same handles when the renderer needs them
// (docs/architecture/adr/0002-rhi.md).
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace raw::rhi {
// Bumped when a backend or a caller must change to keep working.
inline constexpr int kRhiVersion = 1;

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
