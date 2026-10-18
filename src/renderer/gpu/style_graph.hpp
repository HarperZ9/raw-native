#pragma once
// Private to the style checks: a style.wgsl pass added to a frame graph, its accesses declared
// from the generated layout (style_layout.hpp), its pipeline made once per runner. The passes of
// crt-classic, VHS and fibre run as frame graphs of these in one submission.
#include "raw/graph/frame_graph.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace raw::gpu_check::style_graph {

// The workgroup counts (x, y) of a 64-wide pass over n invocations: rows of 32,768 groups, so no
// dimension passes rhi::kMaxDispatchGroups. The pass indexes gid.x + gid.y * 2,097,152.
inline std::uint32_t groupsX(std::uint32_t n) { const std::uint32_t g = (n + 63) / 64; return g < 32768u ? g : 32768u; }
inline std::uint32_t groupsY(std::uint32_t n) { return ((n + 63) / 64 + 32767u) / 32768u; }

struct Passes {
    rhi::Device& dev;
    graph::FrameGraph& g;
    std::map<std::string, rhi::PipelineHandle> pipes;
    // Adds pass `name` over `count` invocations, 64 a group; false (with err) when the build has
    // no such pass or its pipeline fails.
    bool add(const char* name, const std::vector<graph::Resource>& binds, std::uint32_t count, std::string& err);
    // Adds pass `name` dispatched as gx x gy x gz workgroups (passes with workgroup memory).
    bool addGroups(const char* name, const std::vector<graph::Resource>& binds, std::uint32_t gx, std::uint32_t gy, std::uint32_t gz, std::string& err);
};
// Reads `words` 32-bit words of a buffer after the graph ran (the buffer must be MapRead).
bool readWords(rhi::Device& dev, rhi::BufferHandle rb, std::size_t words, std::vector<std::uint32_t>& out, std::string& err);

}  // namespace raw::gpu_check::style_graph
