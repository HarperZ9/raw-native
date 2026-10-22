// The style passes on an RHI device: see style_gpu.hpp.
#include "style_gpu.hpp"
#include "gpu_util.hpp"
#include "shaders/style_layout.hpp"
#include "../data/bluenoise64.hpp"
#include "style_graph.hpp"
#include <algorithm>
#include <cstring>
namespace raw::gpu_check::style_gpu {
namespace {
using namespace util;
using rhi::Access;
using rhi::BufferUsage;

void put3(float* o, const style::V3& v, double w) { o[0] = float(v[0]); o[1] = float(v[1]); o[2] = float(v[2]); o[3] = float(w); }
}  // namespace

// Runs one dispatch of `pass` over n invocations after the uploads, then reads the storage
// buffers `outs` (words[k] words a pixel each) into `dst`. before[k] is binding k's access when
// the submission starts (an uploaded input: CopyDst; a buffer kept from an earlier submission:
// Undefined, since D3D12 buffers decay to the common state between submissions).
bool dispatchAndReadEx(util::Gpu& gpu, int pass, const std::vector<rhi::BufferHandle>& binds, const std::vector<Access>& inAccess,
                       const std::vector<Access>& before, const std::vector<rhi::BufferHandle>& outs, const std::vector<std::uint32_t>& words,
                       uint32_t n, std::vector<std::vector<std::uint32_t>*> dst) {
    if (pass < 0) { gpu.err = "style pass not in style_layout.hpp"; return false; }
    const auto& L = style_passes::kPasses[pass];
    const rhi::PipelineHandle pipe = compute(gpu.dev, L.name, L.binds, L.bindCount, gpu.err);
    if (!pipe.valid() || !gpu.err.empty()) return false;
    std::vector<rhi::BufferHandle> rb;
    for (std::size_t k = 0; k < outs.size(); ++k) rb.push_back(gpu.make(uint64_t(n) * 4 * words[k], BufferUsage::MapRead | BufferUsage::CopyDst, "readback"));
    rhi::CommandList* c = gpu.err.empty() ? gpu.dev.begin(gpu.err) : nullptr;
    if (!c) return false;
    for (auto& f : gpu.pre) f(*c);
    gpu.pre.clear();
    std::vector<rhi::BufferBarrier> in;
    for (std::size_t k = 0; k < binds.size(); ++k) in.push_back({binds[k], before[k], inAccess[k]});
    c->barrier(in);
    c->dispatch(pipe, binds, style_graph::groupsX(std::uint32_t(n)), style_graph::groupsY(std::uint32_t(n)), 1);
    std::vector<rhi::BufferBarrier> after;
    for (std::size_t k = 0; k < outs.size(); ++k) { after.push_back({outs[k], Access::StorageWrite, Access::CopySrc}); after.push_back({rb[k], Access::Undefined, Access::CopyDst}); }
    c->barrier(after);
    for (std::size_t k = 0; k < outs.size(); ++k) c->copyBuffer(outs[k], 0, rb[k], 0, uint64_t(n) * 4 * words[k]);
    if (!gpu.dev.submitAndWait(gpu.err)) return false;
    for (std::size_t k = 0; k < outs.size(); ++k) {
        const std::size_t bytes = std::size_t(n) * 4 * words[k];
        const void* p = gpu.dev.mapRead(rb[k], bytes, gpu.err);
        if (!p) return false;
        dst[k]->resize(std::size_t(n) * words[k]);
        std::memcpy(dst[k]->data(), p, bytes);
        gpu.dev.unmap(rb[k]);
    }
    return true;
}
bool dispatchAndRead(util::Gpu& gpu, int pass, const std::vector<rhi::BufferHandle>& binds, const std::vector<Access>& inAccess,
                     const std::vector<rhi::BufferHandle>& outs, uint32_t n, std::vector<std::vector<std::uint32_t>*> dst) {
    std::vector<Access> before;
    for (Access a : inAccess) before.push_back(a == Access::StorageWrite ? Access::Undefined : Access::CopyDst);
    return dispatchAndReadEx(gpu, pass, binds, inAccess, before, outs, std::vector<std::uint32_t>(outs.size(), 1u), n, dst);
}
int passIndex(const char* name) {
    for (std::size_t k = 0; k < std::size(style_passes::kPasses); ++k)
        if (std::strcmp(style_passes::kPasses[k].name, name) == 0) return int(k);
    return -1;
}

// The dither dispatch alone, warm: inputs in their read state, outputs written in place. Timestamps bracket
// `repeats` dispatches; the interval over repeats is the dispatch's GPU time.
void timeDither(Gpu& gpu, const std::vector<rhi::BufferHandle>& b, DitherTiming& t) {
    const auto& L = style_passes::kPasses[0];
    const rhi::PipelineHandle pipe = compute(gpu.dev, L.name, L.binds, L.bindCount, gpu.err);
    if (!pipe.valid()) { t.error = gpu.err; return; }
    std::vector<double> ms;
    for (int s = 0; s < t.submissions; ++s) {
        rhi::CommandList* c = gpu.dev.begin(gpu.err);
        if (!c) { t.error = gpu.err; return; }
        const rhi::BufferBarrier first[2] = {{b[4], s == 0 ? Access::CopySrc : Access::StorageWrite, Access::StorageWrite}, {b[5], s == 0 ? Access::CopySrc : Access::StorageWrite, Access::StorageWrite}};
        c->barrier(first);
        if (!c->timestamp(0)) { t.error = std::string(gpu.dev.backendName()) + ": no timestamps recorded"; gpu.dev.submitAndWait(gpu.err); return; }
        for (int r = 0; r < t.repeats; ++r) {
            c->dispatch(pipe, b, t.groupsX, t.groupsY, 1);
            const rhi::BufferBarrier again[2] = {{b[4], Access::StorageWrite, Access::StorageWrite}, {b[5], Access::StorageWrite, Access::StorageWrite}};
            c->barrier(again);
        }
        c->timestamp(1);
        if (!gpu.dev.submitAndWait(gpu.err)) { t.error = gpu.err; return; }
        std::vector<double> sec;
        std::string terr;
        if (!gpu.dev.timestampSeconds(2, sec, terr)) { t.error = terr; return; }
        ms.push_back((sec[1] - sec[0]) * 1000.0 / t.repeats);
    }
    std::sort(ms.begin(), ms.end());
    t.ms = ms[ms.size() / 2];
}

bool runDither(rhi::Device& dev, const style::DitherPlan& p, const std::vector<style::DitherLight>& lights, const style::DitherGBuffer& g,
               std::vector<std::uint32_t>& level, std::vector<std::uint32_t>& packed, std::string& err, DitherTiming* timing) {
    if (int(lights.size()) > kMaxDitherLights) { err = "ditherlight: at most 8 lights"; return false; }
    Gpu gpu(dev);
    const uint32_t n = uint32_t(g.w * g.h);
    DitherParamsGpu P{uint32_t(g.w), uint32_t(g.h), uint32_t(p.levels), uint32_t(p.source), uint32_t(p.anchor), uint32_t(lights.size()), 0, 0,
                      {float(p.cell), float(p.px), float(p.pa), float(p.gamma)}, {float(p.dark), float(p.hemi), float(p.fogDist), float(p.fogMax)},
                      {float(p.ink[0]), float(p.ink[1]), float(p.ink[2]), 0}, {float(p.paper[0]), float(p.paper[1]), float(p.paper[2]), 0}};
    std::vector<float> gb(std::size_t(n) * 24, 0.0f), lb(std::size_t(std::max<std::size_t>(1, lights.size())) * 24, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        float* o = gb.data() + i * 24;
        o[0] = g.albedo[i]; o[1] = float(g.kind[i]); o[2] = g.depth[i]; o[3] = g.haze.empty() ? 0.0f : g.haze[i];
        for (std::size_t c = 0; c < 3; ++c) { o[4 + c] = g.wpos[i * 3 + c]; o[8 + c] = g.nrm[i * 3 + c]; o[12 + c] = g.apos[i * 3 + c]; o[16 + c] = g.anrm[i * 3 + c]; }
        for (std::size_t c = 0; c < 4; ++c) o[20 + c] = g.vis[i * 4 + c];
    }
    for (std::size_t k = 0; k < lights.size(); ++k) {
        const style::DitherLight& L = lights[k];
        float* o = lb.data() + k * 24;
        const bool beam = L.kind == style::DitherLight::Kind::Beam;
        o[0] = beam ? 1.0f : 0.0f; o[1] = float(L.intensity); o[2] = float(beam ? L.range : L.radius); o[3] = float(beam ? L.outer : L.core);
        put3(o + 4, L.pos, L.wrap);
        if (beam) { put3(o + 8, L.dir, L.inner); put3(o + 12, L.tan, L.slats); put3(o + 16, L.bit, L.duty); o[20] = float(L.phase); }
    }
    std::vector<std::uint32_t> bn(4096);
    for (std::size_t i = 0; i < 4096; ++i) bn[i] = style::data::kBlueNoise64[i];
    const rhi::BufferHandle ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "dl params");
    const rhi::BufferHandle gbb = gpu.upload(gb.data(), gb.size() * 4, BufferUsage::Storage, "dl gbuffer");
    const rhi::BufferHandle lbb = gpu.upload(lb.data(), lb.size() * 4, BufferUsage::Storage, "dl lights");
    const rhi::BufferHandle bnb = gpu.upload(bn.data(), bn.size() * 4, BufferUsage::Storage, "dl blue noise");
    const rhi::BufferHandle lv = gpu.make(uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "dl levels");
    const rhi::BufferHandle o8 = gpu.make(uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "dl packed");
    const bool ok = dispatchAndRead(gpu, 0, {ub, gbb, lbb, bnb, lv, o8},
                                    {Access::Uniform, Access::StorageRead, Access::StorageRead, Access::StorageRead, Access::StorageWrite, Access::StorageWrite},
                                    {lv, o8}, n, {&level, &packed});
    if (!ok) err = gpu.err.empty() ? "ditherlight dispatch failed" : gpu.err;
    if (ok && timing) { timing->groupsX = style_graph::groupsX(n); timing->groupsY = style_graph::groupsY(n); timeDither(gpu, {ub, gbb, lbb, bnb, lv, o8}, *timing); }
    return ok;
}

bool runPaint(rhi::Device& dev, const std::vector<const style::Projector*>& set, const std::vector<double>& reach, const style::PaintCamera& view,
              const style::PaintOptions& o, const PaintView& g, std::vector<std::uint32_t>& packed, std::string& err) {
    if (int(set.size()) > kMaxProjectors) { err = "paintover: at most 4 projectors"; return false; }
    Gpu gpu(dev);
    const uint32_t n = uint32_t(g.w * g.h);
    const PaintParamsGpu P{uint32_t(g.w), uint32_t(g.h), uint32_t(set.size()), 0,
                           {float(o.facingPow), float(o.fMin), float(o.floor), float(o.tolBase)}, {float(o.tolGrazing), float(o.sampleScale), 0, 0}};
    std::vector<float> gb(std::size_t(n) * 8, 0.0f), cams((set.size() + 1) * 24, 0.0f), dep;
    std::vector<std::uint32_t> img;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < 3; ++c) { gb[i * 8 + c] = g.P[i * 3 + c]; gb[i * 8 + 4 + c] = g.N[i * 3 + c]; }
        gb[i * 8 + 3] = float(g.hit[i]);
    }
    auto cam = [&](std::size_t slot, const style::PaintCamera& c) {
        float* q = cams.data() + slot * 24;
        q[0] = c.ortho ? 1.0f : 0.0f; q[1] = float(c.pos[0]); q[2] = float(c.pos[1]); q[3] = float(c.pos[2]);
        put3(q + 4, c.fwd, c.scale); put3(q + 8, c.right, c.fovY); put3(q + 12, c.up, 0);
    };
    cam(0, view);
    for (std::size_t k = 0; k < set.size(); ++k) {
        const style::Projector& pr = *set[k];
        cam(k + 1, pr.cam);
        float* q = cams.data() + (k + 1) * 24;
        q[16] = float(pr.w); q[17] = float(pr.h); q[18] = float(reach[k]);
        q[20] = float(img.size()); q[21] = float(dep.size());   // word offsets, exact below 2^24
        for (std::size_t i = 0; i < std::size_t(pr.w) * pr.h; ++i) {
            const std::uint8_t* t = pr.rgba.data() + i * 4;
            img.push_back(std::uint32_t(t[0]) | (std::uint32_t(t[1]) << 8) | (std::uint32_t(t[2]) << 16) | (std::uint32_t(t[3]) << 24));
        }
        dep.insert(dep.end(), pr.depth.begin(), pr.depth.end());
    }
    if (img.size() >= (std::size_t(1) << 24)) { err = "paintover: the paintings exceed 2^24 texels"; return false; }
    const rhi::BufferHandle ub = gpu.upload(&P, sizeof P, BufferUsage::Uniform, "po params");
    const rhi::BufferHandle gbb = gpu.upload(gb.data(), gb.size() * 4, BufferUsage::Storage, "po gbuffer");
    const rhi::BufferHandle cb = gpu.upload(cams.data(), cams.size() * 4, BufferUsage::Storage, "po cameras");
    const rhi::BufferHandle ib = gpu.upload(img.data(), img.size() * 4, BufferUsage::Storage, "po paintings");
    const rhi::BufferHandle db = gpu.upload(dep.data(), dep.size() * 4, BufferUsage::Storage, "po depth");
    const rhi::BufferHandle o8 = gpu.make(uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "po packed");
    const bool ok = dispatchAndRead(gpu, 1, {ub, gbb, cb, ib, db, o8},
                                    {Access::Uniform, Access::StorageRead, Access::StorageRead, Access::StorageRead, Access::StorageRead, Access::StorageWrite},
                                    {o8}, n, {&packed});
    if (!ok) err = gpu.err.empty() ? "paintover dispatch failed" : gpu.err;
    return ok;
}

}  // namespace raw::gpu_check::style_gpu
