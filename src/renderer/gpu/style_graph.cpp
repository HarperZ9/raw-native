// style.wgsl passes in a frame graph: see style_graph.hpp.
#include "style_graph.hpp"
#include "gpu_util.hpp"
#include "shaders/style_layout.hpp"
#include "style_gpu.hpp"
#include <cstring>
namespace raw::gpu_check::style_graph {
using rhi::Access;

// One 64-wide group per 64 invocations, folded into rows of 32,768 groups: a 4K frame needs 129,600
// groups, over the 65,535 a dimension every backend allows (rhi::kMaxDispatchGroups). Every
// 64-wide style.wgsl pass indexes gid.x + gid.y * 2,097,152 to match.
bool Passes::add(const char* name, const std::vector<graph::Resource>& binds, std::uint32_t count, std::string& err) {
    return addGroups(name, binds, groupsX(count), groupsY(count), 1, err);
}

bool Passes::addGroups(const char* name, const std::vector<graph::Resource>& binds, std::uint32_t gx, std::uint32_t gy, std::uint32_t gz, std::string& err) {
    const int pi = style_gpu::passIndex(name);
    if (pi < 0) { err = std::string("no style pass ") + name; return false; }
    const auto& L = style_passes::kPasses[pi];
    if (int(binds.size()) != L.bindCount) { err = std::string("style pass ") + name + ": binding count differs from its layout"; return false; }
    auto it = pipes.find(name);
    if (it == pipes.end()) {
        const rhi::PipelineHandle pipe = util::compute(dev, L.name, L.binds, L.bindCount, err);
        if (!pipe.valid()) return false;
        it = pipes.emplace(name, pipe).first;
    }
    const rhi::PipelineHandle pipe = it->second;
    std::vector<graph::Use> uses;
    for (int k = 0; k < L.bindCount; ++k)
        uses.push_back({binds[std::size_t(k)], L.binds[k] == rhi::Binding::Uniform ? Access::Uniform : L.binds[k] == rhi::Binding::StorageRead ? Access::StorageRead : Access::StorageWrite});
    g.addPass(name, uses, [pipe, binds, gx, gy, gz](graph::PassContext& c) {
        std::vector<rhi::BufferHandle> h;
        for (graph::Resource r : binds) h.push_back(c.buffer(r));
        c.cmd->dispatch(pipe, h, gx, gy, gz);
    });
    return true;
}

bool readWords(rhi::Device& dev, rhi::BufferHandle rb, std::size_t words, std::vector<std::uint32_t>& out, std::string& err) {
    const void* q = dev.mapRead(rb, std::uint64_t(words) * 4, err);
    if (!q) return false;
    out.resize(words);
    std::memcpy(out.data(), q, words * 4);
    dev.unmap(rb);
    return true;
}

}  // namespace raw::gpu_check::style_graph
