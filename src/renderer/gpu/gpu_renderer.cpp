// The GPU renderer: the frame as a frame graph of compute passes on the RHI.
// Backend-neutral: the same code runs on D3D12 and WebGPU, and the passes'
// accesses come from the layout generated from the WGSL, so the graph's
// barriers follow the shaders' own declarations.
//
//   upload   params (uniform) and triangles (storage)
//   setup    raster    motion    ssao    [rtao]    shade
//   readback every requested channel into one MapRead buffer, the output
//
// In frame-only mode the readback asks for the shaded frame alone, and the
// graph culls the passes it does not need (motion, and the AO estimator the
// frame is not lit with).
#include "raw/renderer/gpu.hpp"
#include "raw/graph/frame_graph.hpp"
#include "raw/rhi/rhi.hpp"
#include "frame_pack.hpp"
#include "shader_library.hpp"
#include "shaders/pass_layout.hpp"
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace raw {
namespace {
using namespace gpu_host;
using graph::FrameGraph; using graph::Resource; using graph::Use;
using rhi::Access; using rhi::BufferUsage;

const gpu_passes::PassLayout* layoutOf(const char* name){
    for (const auto& p : gpu_passes::kPasses) if (std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}
// One pipeline per pass, created on first use and kept with the process-wide device.
rhi::PipelineHandle pipeline(rhi::Device& dev, const gpu_passes::PassLayout& l, std::string& err){
    static std::map<std::string, rhi::PipelineHandle> cache;
    if (auto it = cache.find(l.name); it != cache.end()) return it->second;
    const rhi::ShaderCode code = gpu_shaders::find(l.name, dev.shaderFormat());
    if (!code.bytes){ err = std::string("pass ") + l.name + " is not compiled into this build"; return {}; }
    rhi::ComputePipelineDesc d{l.name, code, std::span<const rhi::Binding>(l.binds, (std::size_t)l.bindCount)};
    rhi::PipelineHandle p = dev.createComputePipeline(d, err);
    if (p.valid()) cache[l.name] = p;
    return p;
}
Access accessOf(rhi::Binding b){
    return b == rhi::Binding::Uniform ? Access::Uniform
         : b == rhi::Binding::StorageRead ? Access::StorageRead : Access::StorageWrite;
}
// A compute pass: binding i of the shader is binds[i], with the access the
// shader declares for it.
bool addDispatch(FrameGraph& g, rhi::Device& dev, const char* name, std::vector<Resource> binds,
                 uint32_t gx, uint32_t gy, std::string& err){
    const gpu_passes::PassLayout* l = layoutOf(name);
    if (!l){ err = std::string("pass ") + name + " is not in the shader layout"; return false; }
    if ((int)binds.size() != l->bindCount){ err = std::string("pass ") + name + ": binding count differs from the WGSL"; return false; }
    const rhi::PipelineHandle pipe = pipeline(dev, *l, err);
    if (!pipe.valid()) return false;
    std::vector<Use> uses;
    for (int i = 0; i < l->bindCount; ++i) uses.push_back({binds[(size_t)i], accessOf(l->binds[i])});
    g.addPass(name, std::move(uses), [pipe, binds, gx, gy](graph::PassContext& c){
        std::vector<rhi::BufferHandle> h;
        for (Resource r : binds) h.push_back(c.buffer(r));
        c.cmd->dispatch(pipe, h, gx, gy, 1);
    });
    return true;
}
}  // namespace

bool gpuCompiled(){ return std::strcmp(rhi::linkedBackend(), "none") != 0; }
const char* gpuBackendName(){ return rhi::linkedBackend(); }
bool gpuInit(GpuAdapterInfo& info, std::string& err){
    rhi::Device* d = rhi::device(err);
    if (!d) return false;
    info = d->adapter();
    return true;
}

namespace {
// Every buffer of one frame. The graph creates only the ones a kept pass uses.
struct FrameBuffers { Resource P, T, SF, SI, depth, pos, nrm, am, mot, ss, rt, frame, hdr; };
FrameBuffers declareBuffers(FrameGraph& g, uint64_t trisBytes, uint32_t ntri, uint32_t N){
    const BufferUsage S = BufferUsage::Storage | BufferUsage::CopySrc;
    const uint64_t n4 = N * 4ull, n16 = N * 16ull;
    auto b = [&](const char* name, uint64_t size, BufferUsage u){ return g.createBuffer(name, {size, u, name}); };
    return {b("params", 48 * sizeof(float), BufferUsage::Uniform | BufferUsage::CopyDst),
            b("triangles", trisBytes, BufferUsage::Storage | BufferUsage::CopyDst),
            b("setup_f", ntri * 64ull, BufferUsage::Storage), b("setup_i", ntri * 16ull, BufferUsage::Storage),
            b("depth", n4, S), b("position", n16, S), b("normal", n16, S), b("albedo_mask", n16, S),
            b("motion", n16, S), b("ao_ss", n4, S), b("ao_rt", n4, S), b("frame", n16, S), b("hdr", n16, S)};
}
bool addComputePasses(FrameGraph& g, rhi::Device& dev, const FrameBuffers& b, uint32_t ntri, int w, int h,
                      bool rtao, std::string& err){
    const uint32_t gx = (uint32_t)(w + 7) / 8, gy = (uint32_t)(h + 7) / 8;
    return addDispatch(g, dev, "setup", {b.P, b.T, b.SF, b.SI}, (ntri + 63) / 64, 1, err)
        && addDispatch(g, dev, "raster", {b.P, b.T, b.SF, b.SI, b.depth, b.pos, b.nrm, b.am}, gx, gy, err)
        && addDispatch(g, dev, "motion", {b.P, b.pos, b.am, b.mot}, gx, gy, err)
        && addDispatch(g, dev, "ssao", {b.P, b.pos, b.nrm, b.am, b.ss}, gx, gy, err)
        && (!rtao || addDispatch(g, dev, "rtao", {b.P, b.T, b.pos, b.nrm, b.am, b.rt}, gx, gy, err))
        && addDispatch(g, dev, "shade", {b.P, b.nrm, b.am, rtao ? b.rt : b.ss, b.frame, b.hdr}, gx, gy, err);
}
// The read-back: every requested channel, in unpackFrame() order, copied into
// one MapRead buffer, the graph's only output. A size of zero is skipped.
struct Readback { Resource stage; uint64_t size[kChannels]; uint64_t total{0}; };
void addReadback(FrameGraph& g, const FrameBuffers& b, Readback& rb, int w, int h, bool rtao, bool frameOnly){
    const Resource chan[kChannels] = {b.depth, b.pos, b.nrm, b.am, b.mot, b.ss, b.rt, b.frame, b.hdr};
    std::vector<Use> uses;
    for (int i = 0; i < kChannels; ++i){
        rb.size[i] = readBytes(i, w, h, rtao, frameOnly);
        rb.total += rb.size[i];
        if (rb.size[i]) uses.push_back({chan[i], Access::CopySrc});
    }
    rb.stage = g.createBuffer("readback", {rb.total, BufferUsage::MapRead | BufferUsage::CopyDst, "readback"});
    uses.push_back({rb.stage, Access::CopyDst});
    std::vector<Resource> src(chan, chan + kChannels);
    g.addPass("readback", std::move(uses), [&rb, src](graph::PassContext& c){
        uint64_t off = 0;
        for (int i = 0; i < kChannels; ++i){
            if (rb.size[i]) c.cmd->copyBuffer(c.buffer(src[(size_t)i]), 0, c.buffer(rb.stage), off, rb.size[i]);
            off += rb.size[i];
        }
    });
    g.markOutput(rb.stage);
}
}  // namespace

bool renderGpu(const Scene& scene, int w, int h, const Mat4& prevVP,
               const RenderOptions& opts, FrameResult& r, std::string& err, bool frameOnly){
    rhi::Device* dev = rhi::device(err);
    if (!dev) return false;
    const std::vector<float> tris = packTriangles(scene);
    const uint32_t ntri = (uint32_t)(tris.size() / 24), N = (uint32_t)w * (uint32_t)h;
    const uint64_t trisBytes = tris.size() * 4;
    const Mat4 vp = mul(scene.camera.proj(), scene.camera.view());
    float pf[48] = {};
    packParams(pf, scene, vp, prevVP, w, h, ntri, opts.rtao);

    FrameGraph g(dev);
    const FrameBuffers b = declareBuffers(g, trisBytes, ntri, N);
    g.addPass("upload", {{b.P, Access::CopyDst}, {b.T, Access::CopyDst}}, [&](graph::PassContext& c){
        c.cmd->upload(c.buffer(b.P), 0, pf, sizeof pf);
        c.cmd->upload(c.buffer(b.T), 0, tris.data(), trisBytes);
    });
    if (!addComputePasses(g, *dev, b, ntri, w, h, opts.rtao, err)) return false;
    Readback rb;
    addReadback(g, b, rb, w, h, opts.rtao, frameOnly);
    if (!g.execute(err)) return false;

    const auto* bytes = (const unsigned char*)dev->mapRead(g.buffer(rb.stage), rb.total, err);
    if (!bytes) return false;
    const float* f[kChannels]; uint64_t off = 0;
    for (int i = 0; i < kChannels; ++i){ f[i] = rb.size[i] ? (const float*)(bytes + off) : nullptr; off += rb.size[i]; }
    unpackFrame(r, f, w, h, vp, opts, frameOnly);
    dev->unmap(g.buffer(rb.stage));
    return true;
}
}
