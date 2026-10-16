// The post stack's device executor: see post_stack_gpu.hpp.
#include "post_stack_gpu.hpp"
#include "gpu_util.hpp"
#include "style_gpu.hpp"
#include "shaders/style_layout.hpp"
#include <cstring>
namespace raw::gpu_check::post_gpu {
using post_stack::Spec;
using rhi::Access;
using rhi::BufferUsage;

const char* shaderOf(const std::string& pass) {
    if (pass == "exposure") return "style_exposure";
    if (pass == "srgb-decode") return "style_srgb_decode";
    return nullptr;
}
std::vector<float> paramsOf(const Spec& spec) {
    std::vector<float> p(16, 0.0f);
    if (spec.pass == "exposure") p[0] = float(spec.get("ev", 0));
    return p;
}
DeviceStack::~DeviceStack() { for (auto b : history_) dev_.destroyBuffer(b); }

bool DeviceStack::run(const std::vector<Spec>& specs, post_stack::Stage stage, const post_stack::HostFrame& input, int frame, double time,
                      post_stack::HostFrame& out, std::string& err) {
    using namespace post_stack;
    Schedule sc;
    try { sc = schedule(specs, stage, false); } catch (const std::exception& e) { err = e.what(); return false; }
    const std::uint64_t bytes = std::uint64_t(input.rgba.size()) * 4;
    const std::uint32_t n = std::uint32_t(input.w * input.h);
    util::Gpu gpu(dev_);
    const std::vector<float> zeros(input.rgba.size(), 0.0f);
    while (history_.size() < specs.size()) {
        const rhi::BufferHandle h = dev_.createBuffer({bytes, BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, "post history"}, err);
        if (!h.valid()) return false;
        history_.push_back(h);
    }
    graph::FrameGraph g(&dev_);
    const BufferUsage frameUse = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    std::vector<graph::Resource> r(std::size_t(kHist0) + specs.size());
    const rhi::BufferHandle inBuf = gpu.make(bytes, frameUse, "post input");
    r[kIn] = g.importBuffer("post input", inBuf);
    r[kOut] = g.createBuffer("post output", {bytes, frameUse, "post output"});
    r[kPing0] = g.createBuffer("post ping 0", {bytes, frameUse, "post ping 0"});
    r[kPing1] = g.createBuffer("post ping 1", {bytes, frameUse, "post ping 1"});
    r[kHistNext] = g.createBuffer("post history next", {bytes, frameUse, "post history next"});
    for (std::size_t i = 0; i < specs.size(); ++i) r[std::size_t(kHist0) + i] = g.importBuffer("post history " + std::to_string(i), history_[i]);
    // Uniforms, one a step, and the uploads.
    std::vector<PostParamsGpu> P;
    std::vector<rhi::BufferHandle> ubuf;
    std::vector<graph::Resource> ures;
    for (const Step& st : sc.steps) {
        if (st.copy) { ubuf.push_back({}); ures.push_back({}); P.push_back({}); continue; }
        PostParamsGpu u{{float(input.w), float(input.h), float(frame), float(time)}, {}};
        const std::vector<float> p = paramsOf(specs[st.spec]);
        std::memcpy(u.p, p.data(), sizeof u.p);
        P.push_back(u);
        ubuf.push_back(gpu.make(sizeof u, BufferUsage::Uniform | BufferUsage::CopyDst, "post params"));
        ures.push_back(g.importBuffer("post params", ubuf.back()));
    }
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    std::vector<graph::Use> up{{r[kIn], Access::CopyDst}};
    for (const graph::Resource& u : ures) if (u.valid()) up.push_back({u, Access::CopyDst});
    const bool firstFrame = frame == 0;
    if (firstFrame) for (std::size_t i = 0; i < specs.size(); ++i) up.push_back({r[std::size_t(kHist0) + i], Access::CopyDst});
    g.addPass("post upload", up, [&, firstFrame](graph::PassContext& c) {
        c.cmd->upload(inBuf, 0, input.rgba.data(), bytes);
        for (std::size_t k = 0; k < ubuf.size(); ++k) if (ubuf[k].valid()) c.cmd->upload(ubuf[k], 0, &P[k], sizeof(PostParamsGpu));
        if (firstFrame) for (std::size_t i = 0; i < specs.size(); ++i) c.cmd->upload(history_[i], 0, zeros.data(), bytes);
    });
    for (std::size_t k = 0; k < sc.steps.size(); ++k) {
        const Step& st = sc.steps[k];
        const graph::Resource s = r[std::size_t(st.src)], pv = r[std::size_t(st.prev)], d = r[std::size_t(st.dst)];
        if (st.copy) {
            g.addPass("post copy", {{s, Access::CopySrc}, {d, Access::CopyDst}}, [s, d, bytes](graph::PassContext& c) { c.cmd->copyBuffer(c.buffer(s), 0, c.buffer(d), 0, bytes); });
            continue;
        }
        const char* name = shaderOf(specs[st.spec].pass);
        const int pi = name ? style_gpu::passIndex(name) : -1;
        if (pi < 0) { err = "post pass " + specs[st.spec].pass + " has no style.wgsl pass"; return false; }
        const auto& L = style_passes::kPasses[pi];
        const rhi::PipelineHandle pipe = util::compute(dev_, L.name, L.binds, L.bindCount, err);
        if (!pipe.valid()) return false;
        std::vector<graph::Use> uses{{ures[k], Access::Uniform}, {s, Access::StorageRead}};
        if (st.prev != st.src) uses.push_back({pv, Access::StorageRead});
        uses.push_back({d, Access::StorageWrite});
        const graph::Resource u = ures[k];
        g.addPass(std::string("post ") + specs[st.spec].pass, uses, [pipe, u, s, pv, d, n](graph::PassContext& c) {
            const rhi::BufferHandle b[4] = {c.buffer(u), c.buffer(s), c.buffer(pv), c.buffer(d)};
            c.cmd->dispatch(pipe, b, (n + 63) / 64, 1, 1);
        });
    }
    const rhi::BufferHandle rb = gpu.make(bytes, BufferUsage::MapRead | BufferUsage::CopyDst, "post readback");
    const graph::Resource rr = g.importBuffer("post readback", rb), res = r[std::size_t(sc.result)];
    g.addPass("post readback", {{res, Access::CopySrc}, {rr, Access::CopyDst}}, [res, rb, bytes](graph::PassContext& c) { c.cmd->copyBuffer(c.buffer(res), 0, rb, 0, bytes); });
    g.markOutput(rr);
    for (std::size_t i = 0; i < specs.size(); ++i) g.markOutput(r[std::size_t(kHist0) + i]);
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    if (!g.execute(err)) return false;
    const void* p = dev_.mapRead(rb, bytes, err);
    if (!p) return false;
    out = {input.w, input.h, std::vector<float>(input.rgba.size())};
    std::memcpy(out.rgba.data(), p, bytes);
    dev_.unmap(rb);
    return true;
}

}  // namespace raw::gpu_check::post_gpu
