// The RHI validation layer's negative-control suite (ROADMAP M3 criterion 3, checks v1 and v2
// of evidence/m3-aliasing-validation-bounds.json). Each case injects one error into an otherwise
// valid sequence on a recording backend: the layer must report it under its rule and must not
// forward the bad call. The valid sequences must give no report.
#include "raw/rhi/validate.hpp"
#include "check.hpp"
#include <memory>
#include <set>
#include <string>
#include <vector>
using namespace raw::rhi;
namespace {
struct Inner final : Device, CommandList {
    std::vector<std::string> calls;
    uint32_t next{1};
    AdapterInfo info;
    const char* backendName() const override { return "inner"; }
    const AdapterInfo& adapter() const override { return info; }
    ShaderFormat shaderFormat() const override { return ShaderFormat::Wgsl; }
    BufferHandle createBuffer(const BufferDesc&, std::string&) override { return {next++, 1}; }
    void destroyBuffer(BufferHandle) override { calls.push_back("destroyBuffer"); }
    PipelineHandle createComputePipeline(const ComputePipelineDesc&, std::string&) override { return {next++, 1}; }
    TextureHandle createTexture(const TextureDesc&, std::string&) override { return {next++, 1}; }
    void destroyTexture(TextureHandle) override { calls.push_back("destroyTexture"); }
    SamplerHandle createSampler(const SamplerDesc&, std::string&) override { return {next++, 1}; }
    RasterPipelineHandle createRasterPipeline(const RasterPipelineDesc&, std::string&) override { return {next++, 1}; }
    CommandList* begin(std::string&) override { calls.push_back("begin"); return this; }
    bool submitAndWait(std::string&) override { calls.push_back("submit"); return true; }
    const void* mapRead(BufferHandle, uint64_t, std::string&) override { calls.push_back("mapRead"); return this; }
    void unmap(BufferHandle) override { calls.push_back("unmap"); }
    void barrier(std::span<const BufferBarrier> b) override { calls.push_back("barrier " + std::to_string(b.size())); }
    void upload(BufferHandle, uint64_t, const void*, uint64_t) override { calls.push_back("upload"); }
    void copyBuffer(BufferHandle, uint64_t, BufferHandle, uint64_t, uint64_t) override { calls.push_back("copy"); }
    void dispatch(PipelineHandle, std::span<const BufferHandle>, uint32_t, uint32_t, uint32_t) override { calls.push_back("dispatch"); }
    void uploadTexture(TextureHandle, const void*, uint32_t, uint32_t) override { calls.push_back("uploadTexture"); }
    void copyTextureToBuffer(TextureHandle, BufferHandle) override { calls.push_back("copyTexture"); }
    void beginRenderPass(const RenderPassDesc&) override { calls.push_back("beginPass"); }
    void draw(RasterPipelineHandle, std::span<const RasterBind>, uint32_t) override { calls.push_back("draw"); }
    void endRenderPass() override { calls.push_back("endPass"); }
};
const Binding kLayout[] = {Binding::Uniform, Binding::StorageRead, Binding::StorageReadWrite};
const RasterBinding kRaster[] = {RasterBinding::Texture, RasterBinding::Sampler, RasterBinding::StorageRead};

// A valid world: a uniform, an input, an output, a read-back; a pipeline; a texture, a target,
// a sampler and a raster pipeline. Every case starts from here with a list open.
struct World {
    Inner inner;
    std::shared_ptr<ValidationLog> log = std::make_shared<ValidationLog>();
    std::unique_ptr<Device> dev = makeValidatingDevice(inner, log);
    BufferHandle uni, in, out, rb;
    PipelineHandle pipe;
    TextureHandle tex, target;
    SamplerHandle samp;
    RasterPipelineHandle raster;
    CommandList* cmd{nullptr};
    std::string err;
    World(){
        uni = dev->createBuffer({64, BufferUsage::Uniform | BufferUsage::CopyDst, "uniform"}, err);
        in = dev->createBuffer({256, BufferUsage::Storage | BufferUsage::CopyDst, "input"}, err);
        out = dev->createBuffer({256, BufferUsage::Storage | BufferUsage::CopySrc, "output"}, err);
        rb = dev->createBuffer({256, BufferUsage::MapRead | BufferUsage::CopyDst, "readback"}, err);
        pipe = dev->createComputePipeline({"p", {}, kLayout}, err);
        tex = dev->createTexture({4, 4, TextureFormat::RGBA8Unorm, TextureUsage::Sampled | TextureUsage::CopyDst, "tex"}, err);
        target = dev->createTexture({4, 4, TextureFormat::RGBA8Unorm, TextureUsage::RenderTarget | TextureUsage::CopySrc, "target"}, err);
        samp = dev->createSampler({}, err);
        RasterPipelineDesc rd; rd.layout = kRaster;
        raster = dev->createRasterPipeline(rd, err);
        cmd = dev->begin(err);
    }
    std::size_t forwarded(const std::string& call) const {
        std::size_t n = 0;
        for (const auto& c : inner.calls) n += c == call;
        return n;
    }
    void barrier(BufferHandle b, Access before, Access after){ BufferBarrier x{b, before, after}; cmd->barrier({&x, 1}); }
    // The valid frame: upload, compute, copy out, a draw that samples and pulls, submit, map.
    void frame(){
        const unsigned char data[64] = {};
        cmd->upload(uni, 0, data, 64);
        cmd->upload(in, 0, data, 64);
        barrier(uni, Access::CopyDst, Access::Uniform);
        barrier(in, Access::CopyDst, Access::StorageRead);
        const BufferHandle binds[] = {uni, in, out};
        cmd->dispatch(pipe, binds, 1, 1, 1);
        barrier(out, Access::StorageWrite, Access::CopySrc);
        cmd->copyBuffer(out, 0, rb, 0, 256);
        cmd->uploadTexture(tex, data, 4, 4);
        RenderPassDesc pass; pass.color[0].texture = target;
        cmd->beginRenderPass(pass);
        const RasterBind rbinds[] = {{{}, tex, {}}, {{}, {}, samp}, {in, {}, {}}};
        cmd->draw(raster, rbinds, 3);
        cmd->endRenderPass();
        cmd->copyTextureToBuffer(target, dev->createBuffer({textureRowPitch(4) * 4, BufferUsage::CopyDst, "tex out"}, err));
    }
};

// One injected error: `inject` runs inside the valid frame's list; the rule must be reported and
// `call` (the backend call the error would have made) must not have been forwarded more than `allowed`.
template<class F> void caseOf(const char* rule, const char* call, std::size_t allowed, F inject){
    World w;
    inject(w);
    const bool caught = w.log->has(rule);
    if (!caught) std::printf("not caught: %s\n", rule);
    CHECK(caught);
    CHECK(w.forwarded(call) <= allowed);
}
std::set<std::string> covered;
template<class F> void rule(const char* r, const char* call, std::size_t allowed, F inject){ covered.insert(r); caseOf(r, call, allowed, inject); }

void positive(){
    World w;
    w.frame();
    CHECK(w.dev->submitAndWait(w.err));
    CHECK(w.dev->mapRead(w.rb, 256, w.err) != nullptr);
    w.dev->unmap(w.rb);
    for (const auto& r : w.log->reports) std::printf("unexpected %s: %s\n", r.rule.c_str(), r.message.c_str());
    CHECK(w.log->reports.empty());
    CHECK(w.forwarded("dispatch") == 1 && w.forwarded("draw") == 1 && w.forwarded("copy") == 1);
    // A second list starts every buffer Undefined: the same frame is valid again.
    w.cmd = w.dev->begin(w.err);
    w.frame();
    CHECK(w.dev->submitAndWait(w.err) && w.log->reports.empty());
}
void negatives(){
    const unsigned char d[512] = {};
    rule("V01", "upload", 0, [&](World& w){ w.dev->destroyBuffer(w.in); w.cmd->upload(w.in, 0, d, 4); });
    rule("V01", "dispatch", 0, [&](World& w){ const BufferHandle b[] = {w.uni, w.in, BufferHandle{999, 7}}; w.cmd->dispatch(w.pipe, b, 1, 1, 1); });
    rule("V02", "upload", 0, [&](World& w){ w.cmd->upload(w.in, 200, d, 64); });
    rule("V02", "copy", 0, [&](World& w){ w.barrier(w.out, Access::Undefined, Access::CopySrc); w.cmd->copyBuffer(w.out, 0, w.rb, 128, 256); });
    rule("V03", "upload", 0, [&](World& w){ w.cmd->upload(w.out, 0, d, 4); });   // output has no CopyDst
    rule("V03", "mapRead", 0, [&](World& w){ w.dev->submitAndWait(w.err); w.dev->mapRead(w.in, 4, w.err); });
    rule("V03", "dispatch", 0, [&](World& w){ const BufferHandle b[] = {w.in, w.in, w.out}; w.cmd->dispatch(w.pipe, b, 1, 1, 1); });
    rule("V04", "dispatch", 0, [&](World& w){ const BufferHandle b[] = {w.uni, w.in}; w.cmd->dispatch(w.pipe, b, 1, 1, 1); });
    rule("V05", "dispatch", 0, [&](World& w){ const BufferHandle b[] = {w.uni, w.in, w.out}; w.cmd->dispatch(w.pipe, b, 0, 1, 1); });
    rule("V05", "dispatch", 0, [&](World& w){ const BufferHandle b[] = {w.uni, w.in, w.out}; w.cmd->dispatch(w.pipe, b, 70000, 1, 1); });
    rule("V06", "barrier 1", 0, [&](World& w){ w.cmd->upload(w.in, 0, d, 4); w.barrier(w.in, Access::StorageWrite, Access::StorageRead); });
    rule("V07", "dispatch", 0, [&](World& w){   // the input still in copy-dst: no barrier to storage-read
        w.cmd->upload(w.uni, 0, d, 4); w.cmd->upload(w.in, 0, d, 4); w.barrier(w.uni, Access::CopyDst, Access::Uniform);
        const BufferHandle b[] = {w.uni, w.in, w.out}; w.cmd->dispatch(w.pipe, b, 1, 1, 1); });
    rule("V07", "dispatch", 1, [&](World& w){   // two writes of the output with no barrier between
        const BufferHandle b[] = {w.uni, w.in, w.out}; w.cmd->dispatch(w.pipe, b, 1, 1, 1); w.cmd->dispatch(w.pipe, b, 1, 1, 1); });
    rule("V08", "upload", 0, [&](World& w){ w.dev->submitAndWait(w.err); w.cmd->upload(w.in, 0, d, 4); });
    rule("V08", "begin", 1, [&](World& w){ w.dev->begin(w.err); });
    rule("V08", "submit", 1, [&](World& w){ w.dev->submitAndWait(w.err); w.dev->submitAndWait(w.err); });
    rule("V09", "draw", 0, [&](World& w){ const RasterBind b[] = {{{}, w.tex, {}}, {{}, {}, w.samp}, {w.in, {}, {}}}; w.cmd->draw(w.raster, b, 3); });
    rule("V09", "endPass", 0, [&](World& w){ w.cmd->endRenderPass(); });
    rule("V10", "beginPass", 0, [&](World& w){ RenderPassDesc p; p.color[0].texture = w.tex; w.cmd->beginRenderPass(p); });
    rule("V10", "draw", 0, [&](World& w){
        std::string e; TextureHandle f = w.dev->createTexture({4, 4, TextureFormat::RGBA16Float, TextureUsage::RenderTarget, "half"}, e);
        RenderPassDesc p; p.color[0].texture = f; w.cmd->beginRenderPass(p);
        const RasterBind b[] = {{{}, w.tex, {}}, {{}, {}, w.samp}, {w.in, {}, {}}}; w.cmd->draw(w.raster, b, 3); });
    rule("V11", "uploadTexture", 0, [&](World& w){ w.cmd->uploadTexture(w.tex, d, 8, 4); });
    rule("V11", "uploadTexture", 0, [&](World& w){ w.cmd->uploadTexture(w.target, d, 4, 4); });   // no CopyDst
    rule("V12", "copyTexture", 0, [&](World& w){ std::string e; BufferHandle small = w.dev->createBuffer({64, BufferUsage::CopyDst, "small"}, e); w.cmd->copyTextureToBuffer(w.target, small); });
    rule("V12", "copyTexture", 0, [&](World& w){ std::string e; BufferHandle big = w.dev->createBuffer({4096, BufferUsage::CopyDst, "big"}, e); w.cmd->copyTextureToBuffer(w.tex, big); });
    rule("V13", "mapRead", 0, [&](World& w){ w.dev->submitAndWait(w.err); w.dev->mapRead(w.rb, 512, w.err); });
    rule("V13", "mapRead", 0, [&](World& w){
        w.barrier(w.out, Access::Undefined, Access::CopySrc); w.cmd->copyBuffer(w.out, 0, w.rb, 0, 16); w.dev->mapRead(w.rb, 16, w.err); });
    // A list with any report fails its submission, so a run under the layer cannot pass.
    World w;
    w.cmd->upload(w.in, 300, d, 4);
    CHECK(!w.dev->submitAndWait(w.err) && w.err.find("V02") != std::string::npos);
}
}  // namespace

int main(){
    positive();
    negatives();
    CHECK(covered.size() == 13);
    std::printf("rules covered: %zu of 13\n", covered.size());
    return raw_test_summary();
}
