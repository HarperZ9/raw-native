// Hybrid rendering and its checks: see raw/renderer/rt_hybrid.hpp.
#include "raw/renderer/rt_hybrid.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_parity.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "shaders/pt_layout.hpp"
#include "shaders/swr_layout.hpp"
#include "rt_dev.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace raw::gpu_check {
namespace {
using namespace rtdev;
std::vector<float> flatTris(const swr::Geometry& g) {
    std::vector<float> f;
    for (const Tri& t : rt::trianglesOf(g)) for (Vec3 v : {t.a, t.b, t.c}) f.insert(f.end(), {v.x, v.y, v.z});
    return f;
}
std::vector<float> attrsOf(const swr::Geometry& g) {
    std::vector<float> a(g.triangles() * 16, 0.0f);
    for (std::size_t k = 0; k < g.triangles(); ++k)
        for (std::size_t v = 0; v < 3; ++v) {
            const std::uint32_t i = g.idx[k * 3 + v];
            float* x = &a[k * 16 + v * 5];
            x[0] = g.uv[i].x; x[1] = g.uv[i].y; x[2] = g.nrm[i].x; x[3] = g.nrm[i].y; x[4] = g.nrm[i].z;
        }
    return a;
}
// The PtParams words pt_hybrid reads: size, flags, root, eye and sun (rt_pt_gpu.cpp has the full layout).
std::vector<std::uint32_t> hybridParams(const rt::PtScene& ps, const swr::Scene& base, int w, int h, std::uint32_t flags) {
    std::vector<std::uint32_t> p(56, 0u);
    p[0] = std::uint32_t(w); p[1] = std::uint32_t(h); p[5] = flags; p[8] = std::uint32_t(ps.tree.root);
    const Vec3 sd = normalize(ps.sunDir);
    const float eye[4] = {base.eye.x, base.eye.y, base.eye.z, 0.0f}, sun[4] = {sd.x, sd.y, sd.z, 1.0f};
    std::memcpy(&p[12], eye, 16);
    std::memcpy(&p[44], sun, 16);
    return p;
}
}  // namespace

HybridFrame hybridGpu(rhi::Device& device, const rt::PtScene& ps, const swr::Scene& base, int w, int h, std::uint32_t flags) {
    HybridFrame H;
    const swr::Options o;
    const Mat4 M = base.viewProj(w, h);
    const SwrFrame f = swrGpu(device, ps.geo, ps.tex, base.light, M, w, h, o, nullptr);
    if (!f.error.empty()) { H.error = f.error; return H; }
    const std::uint64_t npx = std::uint64_t(w) * std::uint64_t(h);
    std::vector<std::uint32_t> vis(npx * 2);
    for (std::size_t p = 0; p < npx; ++p) { vis[p * 2] = f.vis.slot[p]; vis[p * 2 + 1] = f.vis.depthQ[p]; }
    std::vector<PassInfo> table = passTable(swr_passes::kPasses), pt = passTable(pt_passes::kPasses);
    table.insert(table.end(), pt.begin(), pt.end());
    Dev D{device, H.error, table};
    if (!(D.cl = device.begin(H.error))) return H;
    const std::vector<std::uint32_t> sp = swr::params(o, w, h, std::uint32_t(f.setup.slots()), 16, M, std::uint32_t(ps.geo.triangles()), base.light);
    const std::vector<float> tris = flatTris(ps.geo), attrs = attrsOf(ps.geo);
    std::vector<std::int32_t> ni;
    std::vector<float> nb;
    for (const rt::Node& x : ps.tree.nodes) {
        ni.insert(ni.end(), {x.left, x.right, x.tri, 0});
        nb.insert(nb.end(), {x.lo[0], x.lo[1], x.lo[2], x.hi[0], x.hi[1], x.hi[2], 0.0f, 0.0f});
    }
    const BufferHandle SI = D.upload(f.setup.ints.data(), f.setup.ints.size() * 4, "hy si"), SF = D.upload(f.setup.floats.data(), f.setup.floats.size() * 4, "hy sf");
    const BufferHandle B = D.upload(vis.data(), vis.size() * 4, "hy vis"), VP = D.upload(tris.data(), tris.size() * 4, "hy tris");
    const BufferHandle A = D.upload(attrs.data(), attrs.size() * 4, "hy attrs"), NI = D.upload(ni.data(), ni.size() * 4, "hy ni");
    const BufferHandle NB = D.upload(nb.data(), nb.size() * 4, "hy nb"), G = D.make(npx * 48, kRW, "hy gbuffer"), O = D.make(npx * 16, kRW, "hy out");
    const BufferHandle RG = D.make(npx * 48, kRB, "rb g"), RO = D.make(npx * 16, kRB, "rb o");
    const std::uint32_t gx = std::uint32_t((w + 7) / 8), gy = std::uint32_t((h + 7) / 8);
    D.run("swr_gbuffer", {D.uniform(sp.data(), sp.size()), SI, SF, B, VP, A, G}, gx, gy);
    const std::vector<std::uint32_t> hp = hybridParams(ps, base, w, h, flags);
    D.run("pt_hybrid", {D.uniform(hp.data(), hp.size()), NI, NB, VP, G, O}, gx, gy);
    D.to({G, O}, Access::CopySrc);
    D.cl->copyBuffer(G, 0, RG, 0, npx * 48);
    D.cl->copyBuffer(O, 0, RO, 0, npx * 16);
    if (!device.submitAndWait(H.error)) return H;
    std::vector<float> out;
    if (!D.read(RG, std::size_t(npx) * 12, H.gbuffer) || !D.read(RO, std::size_t(npx) * 4, out)) return H;
    H.lit.resize(npx); H.reflTri.resize(npx); H.reflT.resize(npx);
    for (std::size_t p = 0; p < npx; ++p) { H.lit[p] = out[p * 4]; H.reflTri[p] = int(out[p * 4 + 1]); H.reflT[p] = out[p * 4 + 2]; }
    return H;
}

}  // namespace raw::gpu_check
