// The GPU compute rasterizer from the host: see raw/renderer/swr_parity.hpp. Two submissions:
// setup (optional), bin counts and the scan; then, with the list sized from the read-back
// total, fill, tile visibility and resolve.
#include "raw/renderer/swr_parity.hpp"
#include "shader_library.hpp"
#include "shaders/swr_layout.hpp"
#include <cstring>
namespace raw::gpu_check {
namespace {
using rhi::Access;
using rhi::BufferHandle;
using rhi::BufferUsage;
const BufferUsage kRW = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
const BufferUsage kRB = BufferUsage::MapRead | BufferUsage::CopyDst;
constexpr int kPasses = 6;
const char* const kNames[kPasses] = {"swr_setup", "swr_bin_count", "swr_bin_scan", "swr_bin_fill", "swr_tile", "swr_resolve"};

// One frame's device state: pipelines, buffers (destroyed with it) and sizes.
struct Ctx {
    rhi::Device& dev;
    SwrFrame& F;
    const swr::Geometry& g;
    int w, h;
    std::uint32_t tris{0}, slots{0}, bins{0}, npx{0};
    rhi::PipelineHandle pipe[kPasses];
    std::vector<BufferHandle> all;
    BufferHandle ub, si, sf, cb;
    ~Ctx() { for (auto b : all) dev.destroyBuffer(b); }
    BufferHandle make(std::uint64_t bytes, BufferUsage u, const char* label) {
        const BufferHandle b = dev.createBuffer({bytes < 16 ? 16 : bytes, u, label}, F.error);
        if (b.valid()) all.push_back(b);
        return b;
    }
    template<class T> bool read(BufferHandle b, std::size_t n, std::vector<T>& out) {
        const void* p = dev.mapRead(b, std::uint64_t(n * sizeof(T)), F.error);
        if (!p) return false;
        out.resize(n);
        std::memcpy(out.data(), p, n * sizeof(T));
        dev.unmap(b);
        return true;
    }
    bool pipelines() {
        for (int k = 0; k < kPasses; ++k) {
            const auto& L = swr_passes::kPasses[k];
            if (std::strcmp(L.name, kNames[k]) != 0) { F.error = "swr_layout.hpp is out of order"; return false; }
            const rhi::ShaderCode code = gpu_shaders::find(kNames[k], dev.shaderFormat());
            if (!code.bytes) { F.error = std::string("this build carries no ") + kNames[k] + " shader"; return false; }
            pipe[k] = dev.createComputePipeline({L.name, code, std::span(L.binds, std::size_t(L.bindCount))}, F.error);
            if (!pipe[k].valid()) return false;
        }
        return true;
    }
    bool setupAndBins(const std::vector<std::uint32_t>& params, const swr::Setup* shared);
    bool rasterAndResolve(const swr::TextureSet& t);
    bool unpack(const BufferHandle* rb);
    void recordSetup(rhi::CommandList* cl, const swr::Setup* shared, const std::vector<float>& vpos, BufferHandle vb, BufferHandle xb);
};

// Submission 1's first half: the setup records uploaded (C4) or made by swr_setup (C5).
void Ctx::recordSetup(rhi::CommandList* cl, const swr::Setup* shared, const std::vector<float>& vpos, BufferHandle vb, BufferHandle xb) {
    if (shared) {
        cl->upload(si, 0, shared->ints.data(), shared->ints.size() * 4);
        cl->upload(sf, 0, shared->floats.data(), shared->floats.size() * 4);
        const rhi::BufferBarrier in[3] = {{ub, Access::CopyDst, Access::Uniform}, {si, Access::CopyDst, Access::StorageRead},
                                          {sf, Access::CopyDst, Access::StorageRead}};
        cl->barrier(in);
        return;
    }
    cl->upload(vb, 0, vpos.data(), vpos.size() * 4);
    cl->upload(xb, 0, g.idx.data(), g.idx.size() * 4);
    const rhi::BufferBarrier in[5] = {{ub, Access::CopyDst, Access::Uniform}, {vb, Access::CopyDst, Access::StorageRead},
                                      {xb, Access::CopyDst, Access::StorageRead}, {si, Access::Undefined, Access::StorageWrite},
                                      {sf, Access::Undefined, Access::StorageWrite}};
    cl->barrier(in);
    const BufferHandle b0[5] = {ub, vb, xb, si, sf};
    cl->dispatch(pipe[0], b0, (tris + 63) / 64, 1, 1);
    const rhi::BufferBarrier mid[2] = {{si, Access::StorageWrite, Access::StorageRead}, {sf, Access::StorageWrite, Access::StorageRead}};
    cl->barrier(mid);
}

// Submission 1: the setup records (uploaded, or made by swr_setup), bin counts and offsets.
bool Ctx::setupAndBins(const std::vector<std::uint32_t>& params, const swr::Setup* shared) {
    std::vector<float> vpos;
    for (const Vec3& p : g.pos) { vpos.push_back(p.x); vpos.push_back(p.y); vpos.push_back(p.z); }
    const std::uint64_t siBytes = std::uint64_t(slots) * swr::kSetupInts * 4, sfBytes = std::uint64_t(slots) * swr::kSetupFloats * 4;
    const std::uint64_t cBytes = std::uint64_t(2 * bins + 1) * 4;
    ub = make(params.size() * 4, BufferUsage::Uniform | BufferUsage::CopyDst, "swr params");
    si = make(siBytes, kRW, "swr setup ints"); sf = make(sfBytes, kRW, "swr setup floats"); cb = make(cBytes, kRW, "swr bins");
    const BufferHandle vb = make(vpos.size() * 4, kRW, "swr positions"), xb = make(g.idx.size() * 4, kRW, "swr indices");
    const BufferHandle rsi = make(siBytes, kRB, "rb si"), rsf = make(sfBytes, kRB, "rb sf"), rcb = make(cBytes, kRB, "rb bins");
    rhi::CommandList* cl = F.error.empty() ? dev.begin(F.error) : nullptr;
    if (!cl) return false;
    cl->upload(ub, 0, params.data(), params.size() * 4);
    recordSetup(cl, shared, vpos, vb, xb);
    const rhi::BufferBarrier c0[1] = {{cb, Access::Undefined, Access::StorageWrite}};
    cl->barrier(c0);
    const BufferHandle b1[3] = {ub, si, cb};
    cl->dispatch(pipe[1], b1, (bins + 63) / 64, 1, 1);
    const rhi::BufferBarrier c1[1] = {{cb, Access::StorageWrite, Access::StorageWrite}};
    cl->barrier(c1);
    const BufferHandle b2[2] = {ub, cb};
    cl->dispatch(pipe[2], b2, 1, 1, 1);
    const rhi::BufferBarrier c2[3] = {{cb, Access::StorageWrite, Access::CopySrc}, {si, Access::StorageRead, Access::CopySrc},
                                      {sf, Access::StorageRead, Access::CopySrc}};
    cl->barrier(c2);
    cl->copyBuffer(cb, 0, rcb, 0, cBytes);
    cl->copyBuffer(si, 0, rsi, 0, siBytes);
    cl->copyBuffer(sf, 0, rsf, 0, sfBytes);
    if (!dev.submitAndWait(F.error)) return false;
    std::vector<std::uint32_t> counts;
    F.setup.width = w; F.setup.height = h;
    if (!read(rcb, 2 * bins + 1, counts) || !read(rsi, std::size_t(slots) * swr::kSetupInts, F.setup.ints) ||
        !read(rsf, std::size_t(slots) * swr::kSetupFloats, F.setup.floats)) return false;
    F.listEntries = counts[2 * bins];
    return true;
}

// Attributes as swr_resolve reads them (16 floats a triangle) and the texture table.
void pack(const swr::Geometry& g, const swr::TextureSet& t, std::vector<float>& attrs, std::vector<std::uint32_t>& tex) {
    attrs.assign(g.triangles() * 16, 0.0f);
    for (std::size_t k = 0; k < g.triangles(); ++k) {
        for (std::size_t v = 0; v < 3; ++v) {
            const std::uint32_t i = g.idx[k * 3 + v];
            float* a = &attrs[k * 16 + v * 5];
            a[0] = g.uv[i].x; a[1] = g.uv[i].y; a[2] = g.nrm[i].x; a[3] = g.nrm[i].y; a[4] = g.nrm[i].z;
        }
        attrs[k * 16 + 15] = float(g.triTexture[k]);
    }
    tex.clear();
    for (const auto& e : t.entries) { tex.push_back(std::uint32_t(3 * t.entries.size()) + e.offset); tex.push_back(e.width); tex.push_back(e.height); }
    tex.insert(tex.end(), t.texels.begin(), t.texels.end());
}

// Submission 2: bin lists, tile visibility, resolve; read back into F.vis and F.res.
bool Ctx::rasterAndResolve(const swr::TextureSet& t) {
    std::vector<float> attrs;
    std::vector<std::uint32_t> tex;
    pack(g, t, attrs, tex);
    const std::uint64_t n = npx;
    const BufferHandle qb = make(std::uint64_t(F.listEntries) * 4, kRW, "swr lists"), bb = make(n * 8, kRW, "swr vis");
    const BufferHandle db = make(n * 4, kRW, "swr depth"), ab = make(attrs.size() * 4, kRW, "swr attrs");
    const BufferHandle tb = make(tex.size() * 4, kRW, "swr textures"), ofb = make(n * 16, kRW, "swr out f"), oub = make(n * 8, kRW, "swr out u");
    const BufferHandle rb[4] = {make(n * 8, kRB, "rb vis"), make(n * 4, kRB, "rb depth"), make(n * 16, kRB, "rb of"), make(n * 8, kRB, "rb ou")};
    rhi::CommandList* cl = F.error.empty() ? dev.begin(F.error) : nullptr;
    if (!cl) return false;
    cl->upload(ab, 0, attrs.data(), attrs.size() * 4);
    cl->upload(tb, 0, tex.data(), tex.size() * 4);
    const rhi::BufferBarrier d0[7] = {{si, Access::CopySrc, Access::StorageRead}, {sf, Access::CopySrc, Access::StorageRead},
                                      {cb, Access::CopySrc, Access::StorageRead}, {qb, Access::Undefined, Access::StorageWrite},
                                      {ab, Access::CopyDst, Access::StorageRead}, {tb, Access::CopyDst, Access::StorageRead},
                                      {bb, Access::Undefined, Access::StorageWrite}};
    cl->barrier(d0);
    const BufferHandle b3[4] = {ub, si, cb, qb};
    cl->dispatch(pipe[3], b3, (bins + 63) / 64, 1, 1);
    const rhi::BufferBarrier d1[2] = {{qb, Access::StorageWrite, Access::StorageRead}, {db, Access::Undefined, Access::StorageWrite}};
    cl->barrier(d1);
    const BufferHandle b4[7] = {ub, si, sf, cb, qb, bb, db};
    cl->dispatch(pipe[4], b4, std::uint32_t((w + 7) / 8), std::uint32_t((h + 7) / 8), 1);
    const rhi::BufferBarrier d2[3] = {{bb, Access::StorageWrite, Access::StorageRead}, {ofb, Access::Undefined, Access::StorageWrite},
                                      {oub, Access::Undefined, Access::StorageWrite}};
    cl->barrier(d2);
    const BufferHandle b5[8] = {ub, si, sf, bb, ab, tb, ofb, oub};
    cl->dispatch(pipe[5], b5, std::uint32_t((w + 7) / 8), std::uint32_t((h + 7) / 8), 1);
    const rhi::BufferBarrier d3[4] = {{bb, Access::StorageRead, Access::CopySrc}, {db, Access::StorageWrite, Access::CopySrc},
                                      {ofb, Access::StorageWrite, Access::CopySrc}, {oub, Access::StorageWrite, Access::CopySrc}};
    cl->barrier(d3);
    cl->copyBuffer(bb, 0, rb[0], 0, n * 8);
    cl->copyBuffer(db, 0, rb[1], 0, n * 4);
    cl->copyBuffer(ofb, 0, rb[2], 0, n * 16);
    cl->copyBuffer(oub, 0, rb[3], 0, n * 8);
    if (!dev.submitAndWait(F.error)) return false;
    return unpack(rb);
}

// The read-back buffers of submission 2 into F.vis and F.res.
bool Ctx::unpack(const BufferHandle* rb) {
    std::vector<std::uint32_t> vis, ou;
    std::vector<float> of;
    if (!read(rb[0], npx * 2, vis) || !read(rb[1], npx, F.vis.depth) || !read(rb[2], npx * 4, of) || !read(rb[3], npx * 2, ou)) return false;
    F.vis.width = w; F.vis.height = h;
    F.vis.slot.resize(npx); F.vis.depthQ.resize(npx);
    F.res.uv.resize(std::size_t(npx) * 2); F.res.texelCoord.resize(std::size_t(npx) * 2); F.res.texel.resize(npx); F.res.colour.resize(npx);
    for (std::uint32_t p = 0; p < npx; ++p) {
        F.vis.slot[p] = vis[p * 2]; F.vis.depthQ[p] = vis[p * 2 + 1];
        for (int k = 0; k < 2; ++k) { F.res.uv[p * 2 + k] = of[p * 4 + k]; F.res.texelCoord[p * 2 + k] = of[p * 4 + 2 + k]; }
        F.res.texel[p] = ou[p * 2]; F.res.colour[p] = ou[p * 2 + 1];
    }
    return true;
}
}  // namespace

SwrFrame swrGpu(rhi::Device& dev, const swr::Geometry& g, const swr::TextureSet& t, Vec3 light, const Mat4& M,
                int w, int h, const swr::Options& o, const swr::Setup* shared, int tile) {
    SwrFrame F;
    Ctx c{dev, F, g, w, h};
    c.tris = std::uint32_t(g.triangles());
    c.slots = c.tris * swr::kSlotsPerTri;
    c.bins = std::uint32_t((w + tile - 1) / tile) * std::uint32_t((h + tile - 1) / tile);
    c.npx = std::uint32_t(w * h);
    if (!c.pipelines()) return F;
    if (!c.setupAndBins(swr::params(o, w, h, c.slots, tile, M, c.tris, light), shared)) return F;
    c.rasterAndResolve(t);
    return F;
}

}  // namespace raw::gpu_check
