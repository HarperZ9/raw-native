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
// A device graph may end at a swapchain (presentTo): the present is the frame's last step.
//
// Transient aliasing (ROADMAP M3 criterion 3, evidence/m3-aliasing-validation-bounds.json), off
// by default: transient buffers whose kept lifetimes do not overlap share one RHI buffer, a slot,
// sized to its largest member with the union of their usages. MapRead buffers never share. A
// buffer that takes over a slot is zero-filled before its first use, so its passes see zeros, as
// in a buffer just created. setAliasing(true), or RAW_NATIVE_GRAPH_ALIAS=1 for every graph.
// Not yet here: async compute, split barriers, textures and cross-frame resources.
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
// Transient memory one execute asks the RHI for: without aliasing every created transient is
// its own buffer (unaliased bytes); with it, each slot is one buffer (aliased bytes).
struct MemoryStats { uint64_t unaliasedBytes{0}, aliasedBytes{0}; uint32_t transients{0}, slots{0}; bool aliasing{false}; };

class FrameGraph;
struct PassContext {
    rhi::CommandList* cmd{nullptr};   // null in a host graph
    const FrameGraph* graph{nullptr};
    rhi::BufferHandle buffer(Resource r) const;
};
using PassFn = std::function<void(PassContext&)>;

class FrameGraph {
public:
    explicit FrameGraph(rhi::Device* device = nullptr);
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
    // The frame ends at a swapchain (N2). The returned host resource stands for the backbuffer:
    // passes that draw into swapchain.current() declare a write of it, which keeps them. execute
    // records the backbuffer's move to the present state after the last kept pass, submits and
    // waits, runs beforePresent (a read-back of the presented image; false stops the frame
    // before Present), then presents with syncInterval. A graph presents once per execute.
    Resource presentTo(rhi::Swapchain& swapchain, uint32_t syncInterval,
                       std::function<bool(std::string&)> beforePresent = {});
    // NotPresented until an execute reaches Present; DeviceLost when the submission or Present
    // found the device lost.
    rhi::PresentStatus presentStatus() const { return presentStatus_; }

    // Turn transient aliasing on or off before compile (the default reads RAW_NATIVE_GRAPH_ALIAS).
    void setAliasing(bool on){ aliasing_ = on; compiled_ = false; }
    bool aliasing() const { return aliasing_; }
    MemoryStats memoryStats() const;

    bool compile(std::string& err);
    bool execute(std::string& err);   // compiles first when needed

    rhi::BufferHandle buffer(Resource r) const;

    struct Barrier { Resource resource; rhi::Access before, after; };
    // fills: transients that take over a slot at this step; each is zero-filled after a barrier
    // from the slot's previous state to CopyDst, before the step's own barriers.
    struct Fill { Resource resource; rhi::Access slotBefore; };
    struct Step { uint32_t pass; std::vector<Barrier> barriers; std::vector<Fill> fills; };
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
        rhi::Access firstAccess{rhi::Access::Undefined}, lastAccess{rhi::Access::Undefined};
        int slot{-1};
    };
    struct Slot { rhi::BufferDesc desc; rhi::BufferHandle buffer; int end{-1}; };
    struct Pass { std::string name; std::vector<Use> uses; PassFn fn; bool kept{false}; };
    bool validate(std::string& err) const;
    void cull();
    void planBarriers();
    bool createBuffers(std::string& err);
    void assignSlots();
    void writeStats() const;

    bool finishPresent(std::string& err);
    rhi::Device* device_;
    rhi::Swapchain* swapchain_{nullptr};
    uint32_t syncInterval_{1};
    std::function<bool(std::string&)> beforePresent_;
    rhi::PresentStatus presentStatus_{rhi::PresentStatus::NotPresented};
    std::vector<Res> resources_;
    std::vector<Pass> passes_;
    std::vector<Step> plan_;
    std::vector<Slot> slots_;
    std::vector<unsigned char> zeros_;
    bool compiled_{false}, aliasing_{false}, noFill_{false};
};
const char* accessName(rhi::Access a);
}
