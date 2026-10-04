#pragma once
// The frame graph: every pass of a frame declares which resources it reads and
// writes, then the graph compiles the frame before anything runs.
//
//   setup    createBuffer / importBuffer / createHost, addPass, markOutput
//   compile  validate (no read before a write), cull every pass whose results
//            nothing kept reads and that writes no output, and place a barrier
//            wherever a buffer's access changes or a write must be ordered
//   execute  create the buffers the kept passes use, record each kept pass
//            after its barriers, submit once and wait
//
// A graph with no device is a host graph: its passes run on the CPU in order
// and use host resources only. The CPU reference renderer runs as a host
// graph, so both paths share one pass description (docs/architecture/adr/
// 0003-frame-graph.md). Passes always run in declaration order; the graph never
// reorders them, which keeps the arena's allocation order and every
// certificate unchanged.
//
// Not yet here: transient memory aliasing, async compute, split barriers,
// textures and cross-frame resources. The plan records every resource's first
// and last use, which is what aliasing will need.
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace raw::graph {
struct Resource {
    uint32_t index{UINT32_MAX};
    bool valid() const { return index != UINT32_MAX; }
};
struct Use { Resource resource; rhi::Access access; };

class FrameGraph;
struct PassContext {
    rhi::CommandList* cmd{nullptr};   // null in a host graph
    const FrameGraph* graph{nullptr};
    rhi::BufferHandle buffer(Resource r) const;
};
using PassFn = std::function<void(PassContext&)>;

class FrameGraph {
public:
    explicit FrameGraph(rhi::Device* device = nullptr) : device_(device) {}
    ~FrameGraph();
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    // A buffer the graph owns. It is created at execute only when a kept pass
    // uses it, and destroyed with the graph.
    Resource createBuffer(std::string name, const rhi::BufferDesc& desc);
    // A buffer the caller owns; the graph never creates or destroys it.
    Resource importBuffer(std::string name, rhi::BufferHandle buffer);
    // Data a host pass produces (a CPU image, a counter). No barriers.
    Resource createHost(std::string name);
    // The caller reads this resource after execute, so its writers are kept.
    void markOutput(Resource r);
    void addPass(std::string name, std::vector<Use> uses, PassFn fn);

    bool compile(std::string& err);
    bool execute(std::string& err);   // compiles first when needed

    rhi::BufferHandle buffer(Resource r) const;

    struct Barrier { Resource resource; rhi::Access before, after; };
    struct Step { uint32_t pass; std::vector<Barrier> barriers; };
    // The compiled frame: kept passes in order, each with its barriers.
    const std::vector<Step>& plan() const { return plan_; }
    bool culled(uint32_t pass) const { return pass < passes_.size() && !passes_[pass].kept; }
    std::size_t passCount() const { return passes_.size(); }
    // The compiled graph as text, one line per pass: kept or culled, every use
    // and every barrier, in a fixed order. Equal graphs give equal text.
    std::string describe() const;

private:
    enum class Kind : uint8_t { Transient, Imported, Host };
    struct Res {
        std::string name; Kind kind; rhi::BufferDesc desc; rhi::BufferHandle buffer;
        bool output{false}; int firstUse{-1}, lastUse{-1};
    };
    struct Pass { std::string name; std::vector<Use> uses; PassFn fn; bool kept{false}; };
    bool validate(std::string& err) const;
    void cull();
    void planBarriers();
    bool createBuffers(std::string& err);

    rhi::Device* device_;
    std::vector<Res> resources_;
    std::vector<Pass> passes_;
    std::vector<Step> plan_;
    bool compiled_{false};
};
const char* accessName(rhi::Access a);
}
