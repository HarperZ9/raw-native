// Transient aliasing in the frame graph (ROADMAP M3 criterion 3, check a1 of
// evidence/m3-aliasing-validation-bounds.json). A device that keeps real bytes for every buffer
// and runs uploads at record time stands in for a backend, so slot assignment, the zero fill and
// what a pass sees are checked exactly.
#include "raw/graph/frame_graph.hpp"
#include "check.hpp"
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace raw;
using rhi::Access;
using rhi::BufferUsage;
namespace {
struct Memory final : rhi::Device, rhi::CommandList {
    std::map<uint32_t, std::vector<unsigned char>> bytes;
    std::map<uint32_t, rhi::BufferDesc> descs;
    std::vector<std::string> log;
    uint32_t next{1};
    rhi::AdapterInfo info;
    const char* backendName() const override { return "memory"; }
    const rhi::AdapterInfo& adapter() const override { return info; }
    rhi::ShaderFormat shaderFormat() const override { return rhi::ShaderFormat::Wgsl; }
    rhi::BufferHandle createBuffer(const rhi::BufferDesc& d, std::string&) override {
        bytes[next].assign((std::size_t)d.size, 0); descs[next] = d;
        log.push_back("create " + std::to_string(d.size));
        return {next++, 1};
    }
    void destroyBuffer(rhi::BufferHandle b) override { bytes.erase(b.index); }
    rhi::TextureHandle createTexture(const rhi::TextureDesc&, std::string&) override { return {}; }
    void destroyTexture(rhi::TextureHandle) override {}
    rhi::SamplerHandle createSampler(const rhi::SamplerDesc&, std::string&) override { return {}; }
    rhi::RasterPipelineHandle createRasterPipeline(const rhi::RasterPipelineDesc&, std::string&) override { return {}; }
    rhi::PipelineHandle createComputePipeline(const rhi::ComputePipelineDesc&, std::string&) override { return {1, 1}; }
    void uploadTexture(rhi::TextureHandle, const void*, uint32_t, uint32_t) override {}
    void copyTextureToBuffer(rhi::TextureHandle, rhi::BufferHandle) override {}
    void beginRenderPass(const rhi::RenderPassDesc&) override {}
    void draw(rhi::RasterPipelineHandle, std::span<const rhi::RasterBind>, uint32_t) override {}
    void endRenderPass() override {}
    rhi::CommandList* begin(std::string&) override { return this; }
    bool submitAndWait(std::string&) override { return true; }
    const void* mapRead(rhi::BufferHandle, uint64_t, std::string&) override { return nullptr; }
    void unmap(rhi::BufferHandle) override {}
    void barrier(std::span<const rhi::BufferBarrier> b) override {
        for (const auto& x : b) log.push_back("barrier " + std::to_string(x.buffer.index) + " " +
                                              graph::accessName(x.before) + "->" + graph::accessName(x.after)); }
    void upload(rhi::BufferHandle d, uint64_t off, const void* src, uint64_t n) override {
        std::memcpy(bytes[d.index].data() + off, src, (std::size_t)n);
        log.push_back("upload " + std::to_string(d.index) + " " + std::to_string(n));
    }
    void copyBuffer(rhi::BufferHandle, uint64_t, rhi::BufferHandle, uint64_t, uint64_t) override {}
    void dispatch(rhi::PipelineHandle, std::span<const rhi::BufferHandle>, uint32_t, uint32_t, uint32_t) override {}
};
const BufferUsage S = BufferUsage::Storage;
bool has(const std::vector<std::string>& v, const std::string& x){ for (const auto& e : v) if (e == x) return true; return false; }
void setenv_compat(const char* k, const char* v){
#if defined(_WIN32)
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

// a lives over passes 0..1, b over 2..3, c over 1..2: b may take a's slot, c may not share with
// either. With aliasing off the command log is the same as before aliasing existed.
void lifetimes(bool alias, std::vector<std::string>* logOut, graph::MemoryStats* stats){
    Memory dev;
    graph::FrameGraph g(&dev);
    g.setAliasing(alias);
    auto a = g.createBuffer("a", {256, S, "a"});
    auto b = g.createBuffer("b", {128, S | BufferUsage::CopySrc, "b"});
    auto c = g.createBuffer("c", {64, S, "c"});
    auto out = g.createBuffer("out", {64, BufferUsage::MapRead | BufferUsage::CopyDst, "out"});
    g.addPass("p0", {{a, Access::StorageWrite}}, [](graph::PassContext&){});
    g.addPass("p1", {{a, Access::StorageRead}, {c, Access::StorageWrite}}, [](graph::PassContext&){});
    g.addPass("p2", {{c, Access::StorageRead}, {b, Access::StorageWrite}}, [](graph::PassContext&){});
    g.addPass("p3", {{b, Access::CopySrc}, {out, Access::CopyDst}}, [](graph::PassContext&){});
    g.markOutput(out);
    std::string err;
    CHECK(g.execute(err));
    CHECK(g.buffer(a).valid() && g.buffer(b).valid() && g.buffer(c).valid());
    if (alias){
        CHECK(g.buffer(a) == g.buffer(b));         // disjoint lifetimes share
        CHECK(!(g.buffer(a) == g.buffer(c)));      // overlapping ones never do
        CHECK(!(g.buffer(out) == g.buffer(a)));    // MapRead never shares
        const auto& d = dev.descs[g.buffer(a).index];
        CHECK(d.size == 256);                      // the largest member
        CHECK(rhi::has(d.usage, BufferUsage::CopySrc) && rhi::has(d.usage, BufferUsage::Storage)
              && rhi::has(d.usage, BufferUsage::CopyDst));   // the union, plus CopyDst for the fill
        CHECK(g.describe().find("fill b slot") != std::string::npos);
        CHECK(g.describe().find("fill a") == std::string::npos && g.describe().find("fill c") == std::string::npos);
    } else {
        CHECK(!(g.buffer(a) == g.buffer(b)));
        CHECK(g.describe().find("fill") == std::string::npos);
    }
    *logOut = dev.log;
    *stats = g.memoryStats();
}
void slotsAndFill(){
    std::vector<std::string> off, on;
    graph::MemoryStats so, sa;
    lifetimes(false, &off, &so);
    lifetimes(true, &on, &sa);
    CHECK(so.unaliasedBytes == 256 + 128 + 64 + 64 && so.aliasedBytes == so.unaliasedBytes && !so.aliasing);
    CHECK(sa.unaliasedBytes == so.unaliasedBytes && sa.aliasedBytes == 256 + 64 + 64 && sa.slots == 2);
    // Off: four creates and no upload. On: three creates; b's first step is a barrier from a's last
    // state to copy-dst, an upload of b's 128 bytes, then copy-dst -> storage-write.
    int creates = 0;
    for (const auto& l : off) creates += l.rfind("create", 0) == 0;
    CHECK(creates == 4);
    for (const auto& l : off) CHECK(l.rfind("upload", 0) != 0);
    creates = 0;
    for (const auto& l : on) creates += l.rfind("create", 0) == 0;
    CHECK(creates == 3);
    std::size_t at = 0;
    for (; at < on.size(); ++at) if (on[at].rfind("upload", 0) == 0) break;
    CHECK(at > 0 && at + 1 < on.size());
    if (at > 0 && at + 1 < on.size()){
        // Handles: the unaliased read-back buffer is created first (1), then the slots (2 holds a and b).
        CHECK(on[at - 1] == "barrier 2 storage-read->copy-dst");
        CHECK(on[at] == "upload 2 128");
        // The step's own barriers follow the fill (c's first, in the pass's use order), with b's from copy-dst.
        bool after = false;
        for (std::size_t k = at + 1; k < on.size(); ++k) after = after || on[k] == "barrier 2 copy-dst->storage-write";
        CHECK(after);
        CHECK(!has(off, "barrier 2 copy-dst->storage-write"));
    }
}
// What a pass sees: a writes 0xAB; b, aliased onto a's slot later, must read zeros. The no-fill
// control (check a4) must see a's bytes instead.
int readOfAliased(bool noFill){
    if (noFill) setenv_compat("RAW_NATIVE_GRAPH_ALIAS_NOFILL", "1"); else setenv_compat("RAW_NATIVE_GRAPH_ALIAS_NOFILL", "0");
    Memory dev;
    graph::FrameGraph g(&dev);
    g.setAliasing(true);
    auto a = g.createBuffer("a", {16, S | BufferUsage::CopyDst, "a"});
    auto b = g.createBuffer("b", {16, S, "b"});
    auto keep = g.createBuffer("keep", {16, S, "keep"});
    int seen = -1;
    const unsigned char ab[16] = {0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB};
    g.addPass("write a", {{a, Access::CopyDst}}, [&](graph::PassContext& c){ c.cmd->upload(c.buffer(a), 0, ab, 16); });
    g.addPass("read a", {{a, Access::StorageRead}, {keep, Access::StorageWrite}}, [](graph::PassContext&){});
    g.addPass("read b", {{b, Access::StorageWrite}, {keep, Access::StorageRead}}, [&](graph::PassContext& c){ seen = dev.bytes[c.buffer(b).index][0]; });
    g.markOutput(b);
    std::string err;
    CHECK(g.execute(err));
    CHECK(g.buffer(a) == g.buffer(b));
    setenv_compat("RAW_NATIVE_GRAPH_ALIAS_NOFILL", "0");
    return seen;
}
}  // namespace

int main(){
    slotsAndFill();
    CHECK(readOfAliased(false) == 0);
    CHECK(readOfAliased(true) == 0xAB);
    return raw_test_summary();
}
