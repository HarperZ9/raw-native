// The noise module on the GPU: see raw/renderer/noise_gpu.hpp.
#include "raw/renderer/noise_gpu.hpp"
#include "../data/bluenoise64.hpp"
#include "shader_library.hpp"
#include "shaders/noise_layout.hpp"
#include <algorithm>
#include <cstring>
namespace raw::gpu_check {
namespace {
using rhi::Access; using rhi::BufferHandle; using rhi::BufferUsage;
struct Params { std::uint32_t w, h, func, seed; float scale, z, strength, pad; };
}  // namespace

bool noiseOnGpu(rhi::Device& dev, const NoiseGrid& g, std::vector<float>& out, std::string& err) {
    const auto& L = noise_passes::kPasses[0];
    const rhi::ShaderCode code = gpu_shaders::find(L.name, dev.shaderFormat());
    if (!code.bytes) { err = "this build carries no noise_eval shader"; return false; }
    static rhi::Device* built = nullptr;
    static rhi::PipelineHandle pipe;
    if (built != &dev || !pipe.valid()) {
        pipe = dev.createComputePipeline({L.name, code, std::span(L.binds, std::size_t(L.bindCount))}, err);
        if (!pipe.valid()) return false;
        built = &dev;
    }
    const Params p{g.w, g.h, g.func, g.seed, g.scale, g.z, g.strength, 0.0f};
    std::vector<std::uint32_t> ranks(style::data::kBlueNoise64, style::data::kBlueNoise64 + 4096);
    const std::uint64_t bytes = std::uint64_t(g.w) * g.h * 4;
    std::vector<BufferHandle> owned;
    const auto make = [&](std::uint64_t n, BufferUsage u, const char* label) { const BufferHandle b = dev.createBuffer({std::max<std::uint64_t>(n, 16), u, label}, err); if (b.valid()) owned.push_back(b); return b; };
    const BufferHandle P = make(sizeof p, BufferUsage::Uniform | BufferUsage::CopyDst, "noise params");
    const BufferHandle B = make(ranks.size() * 4, BufferUsage::Storage | BufferUsage::CopyDst, "noise blue");
    const BufferHandle O = make(bytes, BufferUsage::Storage | BufferUsage::CopySrc, "noise out");
    const BufferHandle R = make(bytes, BufferUsage::MapRead | BufferUsage::CopyDst, "noise readback");
    bool ok = P.valid() && B.valid() && O.valid() && R.valid();
    rhi::CommandList* c = ok ? dev.begin(err) : nullptr;
    if (c) {
        c->upload(P, 0, &p, sizeof p);
        c->upload(B, 0, ranks.data(), ranks.size() * 4);
        const rhi::BufferBarrier in[3] = {{P, Access::CopyDst, Access::Uniform}, {B, Access::CopyDst, Access::StorageRead}, {O, Access::Undefined, Access::StorageWrite}};
        c->barrier(in);
        const BufferHandle binds[3] = {P, B, O};
        c->dispatch(pipe, binds, (g.w + 7) / 8, (g.h + 7) / 8, 1);
        const rhi::BufferBarrier mid[2] = {{O, Access::StorageWrite, Access::CopySrc}, {R, Access::Undefined, Access::CopyDst}};
        c->barrier(mid);
        c->copyBuffer(O, 0, R, 0, bytes);
        ok = dev.submitAndWait(err);
        const void* m = ok ? dev.mapRead(R, bytes, err) : nullptr;
        if (m) { out.resize(std::size_t(g.w) * g.h); std::memcpy(out.data(), m, bytes); dev.unmap(R); } else ok = false;
    } else ok = false;
    for (BufferHandle b : owned) dev.destroyBuffer(b);
    return ok;
}

}  // namespace raw::gpu_check
