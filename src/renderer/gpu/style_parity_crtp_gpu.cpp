// The physical CRT's GPU runner: per frame the signal passes (encode; separate and demod unless
// RGB) and the beam pass with the history carried between frames, then on the last frame the halo
// down, halo convolution and compose with the display encode; one submission.
#include "raw/renderer/style_crt.hpp"
#include "gpu_util.hpp"
#include "style_graph.hpp"
#include <cstring>
namespace raw::gpu_check {
using rhi::Access;
using rhi::BufferUsage;

bool runCrtGpu(rhi::Device& dev, const style::CrtPlan& plan, const std::vector<float>& src, int frames, std::vector<std::uint32_t>& packed, std::string& err) {
    util::Gpu gpu(dev);
    std::vector<float> P = style::crtPackParams(plan, 0);
    P.insert(P.end(), plan.taps.begin(), plan.taps.end());
    const std::uint32_t ns = std::uint32_t(plan.N * plan.lines), n = std::uint32_t(plan.outW * plan.outH);
    const std::uint32_t gw = std::uint32_t((plan.outW + plan.halo.q - 1) / plan.halo.q), gh = std::uint32_t((plan.outH + plan.halo.q - 1) / plan.halo.q), ng = gw * gh;
    const std::vector<float> zeros(std::size_t(n) * 8, 0.0f);
    const BufferUsage st = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, un = BufferUsage::Uniform | BufferUsage::CopyDst;
    std::vector<rhi::BufferHandle> ub;
    for (int f = 0; f < frames; ++f) ub.push_back(gpu.make(16, un, "crt frame"));
    const rhi::BufferHandle pb = gpu.make(P.size() * 4, st, "crt params"), kb = gpu.make(plan.halo.weights.size() * 4, st, "crt halo kernel");
    const rhi::BufferHandle sb = gpu.make(src.size() * 4, st, "crt source"), eb = gpu.make(std::uint64_t(ns) * 16, st, "crt enc");
    const rhi::BufferHandle spb = gpu.make(std::uint64_t(ns) * 16, st, "crt sep"), sgb = gpu.make(std::uint64_t(ns) * 16, st, "crt sig");
    const rhi::BufferHandle hb = gpu.make(std::uint64_t(n) * 32, st, "crt history"), emb = gpu.make(std::uint64_t(n) * 16, st, "crt emission");
    const rhi::BufferHandle hdb = gpu.make(std::uint64_t(ng) * 16, st, "crt halo down"), hcb = gpu.make(std::uint64_t(ng) * 16, st, "crt halo");
    const rhi::BufferHandle ob = gpu.make(std::uint64_t(n) * 4, st, "crt packed"), rb = gpu.make(std::uint64_t(n) * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "crt readback");
    if (!gpu.err.empty()) { err = gpu.err; return false; }
    graph::FrameGraph g(&dev);
    auto imp = [&](const char* name, rhi::BufferHandle h) { return g.importBuffer(name, h); };
    std::vector<graph::Resource> U;
    for (int f = 0; f < frames; ++f) U.push_back(imp("frame", ub[std::size_t(f)]));
    const graph::Resource PR = imp("P", pb), KR = imp("kern", kb), SR = imp("src", sb), ER = imp("enc", eb), SPR = imp("sep", spb), SGR = imp("sig", sgb);
    const graph::Resource HR = imp("hist", hb), EMR = imp("em", emb), HDR = imp("hd", hdb), HCR = imp("hc", hcb), O = imp("out8", ob), RB = imp("readback", rb);
    std::vector<graph::Use> up = {{PR, Access::CopyDst}, {KR, Access::CopyDst}, {SR, Access::CopyDst}, {HR, Access::CopyDst}};
    for (const graph::Resource& u : U) up.push_back({u, Access::CopyDst});
    g.addPass("crt upload", up, [&](graph::PassContext& c) {
        for (int f = 0; f < frames; ++f) { const std::uint32_t fr[4] = {std::uint32_t(f), 0, 0, 0}; c.cmd->upload(ub[std::size_t(f)], 0, fr, sizeof fr); }
        c.cmd->upload(pb, 0, P.data(), P.size() * 4); c.cmd->upload(kb, 0, plan.halo.weights.data(), plan.halo.weights.size() * 4);
        c.cmd->upload(sb, 0, src.data(), src.size() * 4); c.cmd->upload(hb, 0, zeros.data(), zeros.size() * 4);
    });
    style_graph::Passes ps{dev, g, {}};
    for (int f = 0; f < frames; ++f) {
        const graph::Resource& u = U[std::size_t(f)];
        if (!ps.add("style_crtp_encode", {u, PR, SR, ER}, ns, err)) return false;
        if (plan.p.mode != 0 && (!ps.add("style_crtp_separate", {u, PR, ER, SPR}, ns, err) || !ps.add("style_crtp_demod", {u, PR, SPR, SGR}, ns, err))) return false;
        if (!ps.add("style_crtp_beam", {u, PR, plan.p.mode == 0 ? ER : SGR, HR, EMR}, n, err)) return false;
    }
    const graph::Resource& last = U.back();
    if (!ps.add("style_crtp_halo_down", {last, PR, EMR, HDR}, ng, err) || !ps.add("style_crtp_halo_conv", {last, PR, KR, HDR, HCR}, ng, err) ||
        !ps.add("style_crtp_compose", {last, PR, EMR, HCR, O}, n, err)) return false;
    g.addPass("crt readback", {{O, Access::CopySrc}, {RB, Access::CopyDst}}, [ob, rb, n](graph::PassContext& c) { c.cmd->copyBuffer(ob, 0, rb, 0, std::uint64_t(n) * 4); });
    g.markOutput(RB);
    if (!g.execute(err)) return false;
    return style_graph::readWords(dev, rb, n, packed, err);
}

}  // namespace raw::gpu_check
