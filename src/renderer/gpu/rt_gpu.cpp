// The compute PLOC build and traversal from the host: see raw/renderer/rt_gpu.hpp. One
// submission sorts and makes the leaves; then one submission a clustering round, whose
// read-back survivor count decides the next.
#include "raw/renderer/rt_gpu.hpp"
#include "shader_library.hpp"
#include "shaders/rt_layout.hpp"
#include "rt_dev.hpp"
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <map>
namespace raw::gpu_check {
namespace {
using namespace rtdev;
std::vector<float> flatten(const std::vector<Tri>& tris) {
    std::vector<float> f;
    f.reserve(tris.size() * 9);
    for (const Tri& t : tris) for (Vec3 v : {t.a, t.b, t.c}) { f.push_back(v.x); f.push_back(v.y); f.push_back(v.z); }
    return f;
}
}  // namespace

float robustFarScale() { return rt::kSlabExit; }

RtBuild buildPlocGpu(rhi::Device& device, const std::vector<Tri>& tris, int radius) {
    RtBuild B;
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint32_t n = std::uint32_t(tris.size()), nodes = 2 * n - 1, chunks = (n + kChunk - 1) / kChunk;
    std::uint32_t count = 1;
    while (count < n) count <<= 1;
    Dev D{device, B.error, passTable(rt_passes::kPasses)};
    if (!n) return B;
    if (!(D.cl = device.begin(B.error))) return B;
    const std::vector<float> flat = flatten(tris);
    const BufferHandle T = D.upload(flat.data(), flat.size() * 4, "rt tris");
    const BufferHandle G = D.make((8 + std::uint64_t(chunks) * 6) * 4, kRW, "rt globals"), K = D.make(std::uint64_t(count) * 4, kRW, "rt keys");
    const BufferHandle V = D.make(std::uint64_t(count) * 4, kRW, "rt values"), NI = D.make(std::uint64_t(nodes) * 16, kRW, "rt node ints");
    const BufferHandle NB = D.make(std::uint64_t(nodes) * 32, kRW, "rt node boxes");
    BufferHandle C[2] = {D.make((std::uint64_t(n) + 2) * 4, kRW, "rt clusters a"), D.make((std::uint64_t(n) + 2) * 4, kRW, "rt clusters b")};
    const BufferHandle NN = D.make(std::uint64_t(n) * 4, kRW, "rt nn"), S = D.make(std::uint64_t(n) * 4, kRW, "rt merge scan");
    const BufferHandle SV = D.make(std::uint64_t(n) * 4, kRW, "rt survivor scan"), SUM = D.make((std::uint64_t(chunks) + 1) * 4, kRW, "rt sums");
    const BufferHandle RB = D.make(16, kRB, "rt readback");
    std::uint32_t w[12];
    D.params(w, n, n, std::uint32_t(radius), n, 0, 0, count, 0, 1.0f);
    const BufferHandle u0 = D.uniform(w, 12);
    D.run("rt_bounds_chunk", {u0, T, G}, (chunks + 63) / 64);
    D.run("rt_bounds_final", {u0, G}, 1);
    D.run("rt_morton", {u0, T, G, K, V}, (count + 63) / 64);
    for (std::uint32_t k = 2; k <= count; k <<= 1)
        for (std::uint32_t j = k >> 1; j > 0; j >>= 1) {
            D.params(w, n, n, 0, 0, k, j, count, 0, 1.0f);
            D.run("rt_sort_step", {D.uniform(w, 12), K, V}, (count + 63) / 64);
        }
    D.run("rt_leaves", {u0, T, V, NI, NB, C[0]}, (n + 63) / 64);
    if (!device.submitAndWait(B.error)) return B;
    std::uint32_t m = n, next = n, in = 0;
    while (m > 1 && B.error.empty()) {
        if (!(D.cl = device.begin(B.error))) return B;
        D.params(w, n, m, std::uint32_t(radius), next, 0, 0, m, 0, 1.0f);
        const BufferHandle ub = D.uniform(w, 12);
        D.run("rt_ploc_nn", {ub, NB, C[in], NN}, (m + 63) / 64);
        D.run("rt_ploc_flags", {ub, NN, S, SV}, (m + 63) / 64);
        D.scan(S, SUM, ub, m);
        D.scan(SV, SUM, ub, m);
        D.run("rt_ploc_merge", {ub, NN, C[in], S, SV, NI, NB, C[1 - in]}, (m + 63) / 64);
        D.to({C[1 - in]}, Access::CopySrc);
        D.cl->copyBuffer(C[1 - in], std::uint64_t(m) * 4, RB, 0, 8);
        D.cl->copyBuffer(C[1 - in], 0, RB, 8, 4);
        if (!device.submitAndWait(B.error)) return B;
        std::vector<std::uint32_t> r;
        if (!D.read(RB, 3, r)) return B;
        if (r[0] == 0 || r[0] >= m) { B.error = "PLOC round made no merge"; return B; }   // never silent
        m = r[0]; next += r[1]; in = 1 - in; ++B.tree.rounds;
        B.tree.root = int(r[2]);
    }
    if (n == 1) B.tree.root = 0;
    // Read the nodes back.
    if (!(D.cl = device.begin(B.error))) return B;
    const BufferHandle rni = D.make(std::uint64_t(nodes) * 16, kRB, "rb ni"), rnb = D.make(std::uint64_t(nodes) * 32, kRB, "rb nb");
    D.to({NI, NB}, Access::CopySrc);
    D.cl->copyBuffer(NI, 0, rni, 0, std::uint64_t(nodes) * 16);
    D.cl->copyBuffer(NB, 0, rnb, 0, std::uint64_t(nodes) * 32);
    if (!device.submitAndWait(B.error)) return B;
    std::vector<std::int32_t> ni;
    std::vector<float> nb;
    if (!D.read(rni, std::size_t(nodes) * 4, ni) || !D.read(rnb, std::size_t(nodes) * 8, nb)) return B;
    B.tree.nodes.resize(nodes);
    for (std::uint32_t i = 0; i < nodes; ++i) {
        rt::Node& x = B.tree.nodes[i];
        x.left = ni[i * 4]; x.right = ni[i * 4 + 1]; x.tri = ni[i * 4 + 2];
        for (int k = 0; k < 3; ++k) { x.lo[k] = nb[i * 8 + k]; x.hi[k] = nb[i * 8 + 3 + k]; }
    }
    B.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return B;
}

RtTrace traceGpu(rhi::Device& device, const rt::Tree& tree, const std::vector<Tri>& tris, const std::vector<Ray>& rays,
                 const std::vector<float>& tMax, bool any, float farScale) {
    RtTrace R;
    Dev D{device, R.error, passTable(rt_passes::kPasses)};
    const std::uint32_t n = std::uint32_t(rays.size()), nodes = std::uint32_t(tree.nodes.size());
    std::vector<std::int32_t> ni(std::size_t(nodes) * 4, 0);
    std::vector<float> nb(std::size_t(nodes) * 8, 0.0f), rr(std::size_t(n) * 8, 0.0f);
    for (std::uint32_t i = 0; i < nodes; ++i) {
        const rt::Node& x = tree.nodes[i];
        ni[i * 4] = x.left; ni[i * 4 + 1] = x.right; ni[i * 4 + 2] = x.tri;
        for (int k = 0; k < 3; ++k) { nb[i * 8 + k] = x.lo[k]; nb[i * 8 + 3 + k] = x.hi[k]; }
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        const Ray& r = rays[i];
        const float v[7] = {r.o.x, r.o.y, r.o.z, r.d.x, r.d.y, r.d.z, tMax[i]};
        std::memcpy(&rr[std::size_t(i) * 8], v, sizeof v);
    }
    if (!(D.cl = device.begin(R.error))) return R;
    const std::vector<float> flat = flatten(tris);
    const BufferHandle NI = D.upload(ni.data(), ni.size() * 4, "rt ni"), NB = D.upload(nb.data(), nb.size() * 4, "rt nb");
    const BufferHandle T = D.upload(flat.data(), flat.size() * 4, "rt tris"), RY = D.upload(rr.data(), rr.size() * 4, "rt rays");
    const BufferHandle O = D.make(std::uint64_t(n) * 16, kRW, "rt hits"), RB = D.make(std::uint64_t(n) * 16, kRB, "rb hits");
    std::uint32_t w[12];
    D.params(w, std::uint32_t(tris.size()), 0, 0, std::uint32_t(tree.root), 0, 0, n, any ? 1u : 0u, farScale);
    D.run("rt_trace", {D.uniform(w, 12), NI, NB, T, RY, O}, (n + 63) / 64);
    D.to({O}, Access::CopySrc);
    D.cl->copyBuffer(O, 0, RB, 0, std::uint64_t(n) * 16);
    if (!device.submitAndWait(R.error)) return R;
    std::vector<float> o;
    if (!D.read(RB, std::size_t(n) * 4, o)) return R;
    R.t.resize(n); R.tri.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        R.t[i] = o[i * 4]; R.tri[i] = int(o[i * 4 + 1]);
        R.overflows += R.tri[i] == -2;
    }
    return R;
}

}  // namespace raw::gpu_check
