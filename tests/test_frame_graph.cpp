// The frame graph without a GPU: a recording RHI device stands in for a
// backend, so validation, culling, barrier placement and buffer lifetimes are
// checked exactly. The real backends are checked by the identity matrix.
#include "raw/graph/frame_graph.hpp"
#include "check.hpp"
#include <string>
#include <vector>
using namespace raw;
using rhi::Access;
namespace {
struct Recorder final : rhi::Device, rhi::CommandList {
    std::vector<std::string> log;
    uint32_t next{1};
    int live{0};
    rhi::AdapterInfo info;
    const char* backendName() const override { return "recorder"; }
    const rhi::AdapterInfo& adapter() const override { return info; }
    rhi::ShaderFormat shaderFormat() const override { return rhi::ShaderFormat::Wgsl; }
    rhi::BufferHandle createBuffer(const rhi::BufferDesc& d, std::string&) override {
        log.push_back(std::string("create ") + d.label); ++live; return {next++, 1}; }
    void destroyBuffer(rhi::BufferHandle) override { --live; }
    rhi::PipelineHandle createComputePipeline(const rhi::ComputePipelineDesc&, std::string&) override { return {1, 1}; }
    rhi::CommandList* begin(std::string&) override { log.push_back("begin"); return this; }
    bool submitAndWait(std::string&) override { log.push_back("submit"); return true; }
    const void* mapRead(rhi::BufferHandle, uint64_t, std::string&) override { return nullptr; }
    void unmap(rhi::BufferHandle) override {}
    void barrier(std::span<const rhi::BufferBarrier> b) override {
        for (const auto& x : b) log.push_back("barrier " + std::to_string(x.buffer.index) + " " +
                                              graph::accessName(x.before) + "->" + graph::accessName(x.after)); }
    void upload(rhi::BufferHandle d, uint64_t, const void*, uint64_t) override { log.push_back("upload " + std::to_string(d.index)); }
    void copyBuffer(rhi::BufferHandle s, uint64_t, rhi::BufferHandle d, uint64_t, uint64_t) override {
        log.push_back("copy " + std::to_string(s.index) + "->" + std::to_string(d.index)); }
    void dispatch(rhi::PipelineHandle, std::span<const rhi::BufferHandle>, uint32_t, uint32_t, uint32_t) override {
        log.push_back("dispatch"); }
};
bool has(const std::vector<std::string>& v, const std::string& s){
    for (const auto& x : v) if (x == s) return true;
    return false;
}
rhi::BufferDesc desc(const char* label){ return {64, rhi::BufferUsage::Storage, label}; }

// A read before any write is refused at compile.
void readBeforeWrite(){
    {
        graph::FrameGraph g;
        auto a = g.createHost("a");
        g.addPass("reader", {{a, Access::StorageRead}}, [](graph::PassContext&){});
        std::string err;
        CHECK(!g.compile(err));
        CHECK(err.find("reads a before any pass writes it") != std::string::npos);
    }
}
// The same resource twice in one pass is refused; so is a buffer in a host graph.
void malformed(){
    {
        graph::FrameGraph g;
        auto a = g.createHost("a");
        g.addPass("twice", {{a, Access::StorageWrite}, {a, Access::StorageRead}}, [](graph::PassContext&){});
        std::string err;
        CHECK(!g.compile(err) && err.find("twice") != std::string::npos);
        graph::FrameGraph h;
        h.createBuffer("b", desc("b"));
        CHECK(!h.compile(err) && err.find("no device") != std::string::npos);
    }
}
// A host graph runs kept passes in declaration order and culls a pass whose
// result nothing reads.
void hostCulling(){
    {
        graph::FrameGraph g;
        auto a = g.createHost("a"), b = g.createHost("b"), unused = g.createHost("unused");
        std::string order;
        g.addPass("first", {{a, Access::StorageWrite}}, [&](graph::PassContext& c){ order += "1"; CHECK(c.cmd == nullptr); });
        g.addPass("dead", {{a, Access::StorageRead}, {unused, Access::StorageWrite}}, [&](graph::PassContext&){ order += "x"; });
        g.addPass("second", {{a, Access::StorageRead}, {b, Access::StorageWrite}}, [&](graph::PassContext&){ order += "2"; });
        g.markOutput(b);
        std::string err;
        CHECK(g.execute(err));
        CHECK(order == "12");
        CHECK(g.culled(1) && !g.culled(0) && !g.culled(2));
        CHECK(g.describe() == "pass first kept: a=storage-write\n"
                              "pass dead culled: a=storage-read unused=storage-write\n"
                              "pass second kept: a=storage-read b=storage-write\n");
    }
}
// On a device: upload, two dependent compute passes and a read-back. Buffers
// are created only when a kept pass uses them, barriers follow every access
// change and every write, and the graph destroys what it created.
void deviceGraph(){
    Recorder dev;
    {
        graph::FrameGraph g(&dev);
        auto in = g.createBuffer("in", desc("in")), mid = g.createBuffer("mid", desc("mid"));
        auto skipped = g.createBuffer("skipped", desc("skipped")), out = g.createBuffer("out", desc("out"));
        auto stage = g.createBuffer("stage", {64, rhi::BufferUsage::MapRead | rhi::BufferUsage::CopyDst, "stage"});
        g.addPass("upload", {{in, Access::CopyDst}}, [](graph::PassContext& c){ c.cmd->upload(c.buffer({0}), 0, nullptr, 0); });
        g.addPass("a", {{in, Access::StorageRead}, {mid, Access::StorageWrite}}, [](graph::PassContext& c){ c.cmd->dispatch({}, {}, 1, 1, 1); });
        g.addPass("unread", {{mid, Access::StorageRead}, {skipped, Access::StorageWrite}}, [](graph::PassContext& c){ c.cmd->dispatch({}, {}, 1, 1, 1); });
        g.addPass("b", {{in, Access::StorageRead}, {mid, Access::StorageRead}, {out, Access::StorageWrite}},
                  [](graph::PassContext& c){ c.cmd->dispatch({}, {}, 1, 1, 1); });
        g.addPass("readback", {{out, Access::CopySrc}, {stage, Access::CopyDst}},
                  [&](graph::PassContext& c){ c.cmd->copyBuffer(c.buffer(out), 0, c.buffer(stage), 0, 64); });
        g.markOutput(stage);
        std::string err;
        CHECK(g.execute(err));
        CHECK(g.culled(2));
        CHECK(!has(dev.log, "create skipped"));
        CHECK(has(dev.log, "create in") && has(dev.log, "create stage"));
        // in: 1, mid: 2, out: 3, stage: 4 (creation order, skipped never created)
        const std::vector<std::string> want = {
            "create in", "create mid", "create out", "create stage", "begin",
            "barrier 1 undefined->copy-dst", "upload 1",
            "barrier 1 copy-dst->storage-read", "barrier 2 undefined->storage-write", "dispatch",
            "barrier 2 storage-write->storage-read", "barrier 3 undefined->storage-write", "dispatch",
            "barrier 3 storage-write->copy-src", "barrier 4 undefined->copy-dst", "copy 3->4",
            "submit"};
        CHECK(dev.log == want);
        if (dev.log != want) for (const auto& l : dev.log) std::printf("  %s\n", l.c_str());
        // in is read twice in a row (a, then b) with no barrier between the reads.
        CHECK(dev.live == 4);
    }
    CHECK(dev.live == 0);
}
}  // namespace

int main(){
    readBeforeWrite();
    malformed();
    hostCulling();
    deviceGraph();
    return raw_test_summary();
}
