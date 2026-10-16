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

struct Passes {
    rhi::Device& dev;
    graph::FrameGraph& g;
    std::map<std::string, rhi::PipelineHandle> pipes;
    // Adds pass `name` over `count` invocations, 64 a group; false (with err) when the build has
    // no such pass or its pipeline fails.
    bool add(const char* name, const std::vector<graph::Resource>& binds, std::uint32_t count, std::string& err);
};
// Reads `words` 32-bit words of a buffer after the graph ran (the buffer must be MapRead).
bool readWords(rhi::Device& dev, rhi::BufferHandle rb, std::size_t words, std::vector<std::uint32_t>& out, std::string& err);

}  // namespace raw::gpu_check::style_graph
