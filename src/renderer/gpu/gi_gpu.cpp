// Split radiance cascades on the GPU: see raw/renderer/gi_gpu.hpp. The passes are gi.wgsl and
// gi_merge.wgsl (gi_common.wgsl in front); the probe allocation uses rt.wgsl's prefix sum.
#include "raw/renderer/gi_gpu.hpp"
#include "../gi_internal.hpp"
#include "shaders/gi_layout.hpp"
#include "shaders/rt_layout.hpp"
#include "rt_dev.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
namespace raw::gi {
namespace {
using namespace gpu_check::rtdev;
// The host image of GiParams (gi_common.wgsl): 12 words, then 16-byte vectors.
struct Uni {
    std::uint32_t w, h, stride, frame, rw, rh, root, cascade, count, control, nsub, pad0;
    float origin[4], light[4], cell[4], rad[4], misc[4];
    std::uint32_t cdim[4], d[kCascades][4], sbase[4], nbin[4], abase[4], rbase[4];
};
struct State {
    Uni u{};
    std::vector<Cascade> cpu;                      // the CPU's probe tables, for the slot check
    std::uint64_t keys{0}, cacheKeys{0}, accWords{0}, rmFloats{0};
    std::uint32_t slots[kCascades]{}, rays{0};
    BufferHandle GB, NI, NB, TA, SLOT, MARK, SUMS, KEYS, CE, CS, RR, ACC, RM, SUB, SH, OUT;
};
Uni base(const Scene3& s, const GBuffer& g, const Params& p, const std::vector<Cascade>& cs, const Grid& cache){
    Uni u{};
    u.w = std::uint32_t(g.w); u.h = std::uint32_t(g.h); u.stride = std::uint32_t(p.stride);
    u.rw = std::uint32_t(detail::raysAcross(g.w, p.stride)); u.rh = std::uint32_t(detail::raysAcross(g.h, p.stride));
    u.root = std::uint32_t(s.tree.root);
    const float o[4] = {s.lo.x, s.lo.y, s.lo.z, p.cacheCell}, l[4] = {s.toLight.x, s.toLight.y, s.toLight.z, s.li};
    std::memcpy(u.origin, o, 16); std::memcpy(u.light, l, 16);
    u.misc[0] = p.sky;
    u.cdim[0] = cache.nx; u.cdim[1] = cache.ny; u.cdim[2] = cache.nz;
    std::uint64_t keyBase = 0;
    for (int c = 0; c < kCascades; ++c){
        const Grid& G = cs[std::size_t(c)].grid;
        u.cell[c] = G.cell; u.rad[c] = p.radius[c + 1]; u.nbin[c] = cs[std::size_t(c)].bins;
        u.d[c][0] = G.nx; u.d[c][1] = G.ny; u.d[c][2] = G.nz; u.d[c][3] = std::uint32_t(keyBase);
        keyBase += G.keys();
    }
    return u;
}
// One uniform for a dispatch: the state's words with this dispatch's frame, cascade and count.
BufferHandle uni(Dev& D, const State& S, std::uint32_t frame, std::uint32_t cascade, std::uint64_t count, std::uint32_t control = 0){
    Uni u = S.u;
    u.frame = frame; u.cascade = cascade; u.count = std::uint32_t(count); u.control = control;
    return D.uniform(reinterpret_cast<const std::uint32_t*>(&u), sizeof(Uni) / 4);
}
// A one-dimensional dispatch of `count` threads, folded into rows of 65,535 groups of 64.
void run1(Dev& D, const char* name, std::initializer_list<BufferHandle> binds, std::uint64_t count){
    const std::uint64_t groups = std::max<std::uint64_t>(1, (count + 63) / 64);
    D.run(name, binds, std::uint32_t(std::min<std::uint64_t>(groups, 65535)), std::uint32_t((groups + 65534) / 65535));
}
void clear(Dev& D, const State& S, BufferHandle b, std::uint64_t words){ run1(D, "gi_clear", {uni(D, S, 0, 0, words), b}, words); }
bool restart(Dev& D, rhi::Device& dev){ return D.err.empty() && dev.submitAndWait(D.err) && (D.cl = dev.begin(D.err)) != nullptr; }

void upload(Dev& D, State& S, const Scene3& s, const GBuffer& g){
    std::vector<float> gb(std::size_t(g.w) * std::size_t(g.h) * 8, 0.0f), ta;
    for (std::size_t i = 0; i < gb.size() / 8; ++i){
        const Vec3 p = g.position.px[i], n = g.normal.px[i];
        const float v[8] = {p.x, p.y, p.z, g.mask.px[i] ? 1.0f : 0.0f, n.x, n.y, n.z, 0.0f};
        std::memcpy(&gb[i * 8], v, 32);
    }
    for (std::size_t t = 0; t < s.tris.size(); ++t){
        for (Vec3 v : {s.tris[t].a, s.tris[t].b, s.tris[t].c}) ta.insert(ta.end(), {v.x, v.y, v.z});
        for (int k = 0; k < 3; ++k){ const Vec3 n = s.nrm[3 * t + std::size_t(k)]; ta.insert(ta.end(), {n.x, n.y, n.z}); }
        ta.insert(ta.end(), {s.albedo[t].x, s.albedo[t].y, s.albedo[t].z});
    }
    std::vector<std::int32_t> ni;
    std::vector<float> nb;
    for (const rt::Node& x : s.tree.nodes){
        ni.insert(ni.end(), {x.left, x.right, x.tri, 0});
        nb.insert(nb.end(), {x.lo[0], x.lo[1], x.lo[2], x.hi[0], x.hi[1], x.hi[2], 0.0f, 0.0f});
    }
    S.GB = D.upload(gb.data(), gb.size() * 4, "gi gbuffer"); S.TA = D.upload(ta.data(), ta.size() * 4, "gi triangles");
    S.NI = D.upload(ni.data(), ni.size() * 4, "gi nodes"); S.NB = D.upload(nb.data(), nb.size() * 4, "gi boxes");
    std::vector<float> sub;
    for (const detail::SubDir& d : detail::subDirections(std::uint32_t(S.u.nbin[0])))
        sub.insert(sub.end(), {float(d.bin), float(d.x), float(d.y), float(d.z), float(d.w)});
    S.u.nsub = std::uint32_t(sub.size() / 5);
    S.SUB = D.upload(sub.data(), sub.size() * 4, "gi sub-directions");
}
// The probes: mark, prefix sum and keys on the GPU; the slot table read back and checked against the CPU's.
bool allocate(Dev& D, rhi::Device& dev, State& S, GpuRun& r){
    const std::uint64_t chunks = (S.keys + kChunk - 1) / kChunk;
    S.SLOT = D.make(S.keys * 4, kRW, "gi slots"); S.MARK = D.make(S.keys * 4, kRW, "gi marks");
    S.SUMS = D.make((chunks + 1) * 4, kRW, "gi scan sums"); S.KEYS = D.make(std::uint64_t(S.u.w) * S.u.h * 4 * 4, kRW, "gi keys");
    const BufferHandle RB = D.make(S.keys * 4, kRB, "gi slots readback");
    clear(D, S, S.SLOT, S.keys);
    D.run("gi_mark", {uni(D, S, 0, 0, 0), S.GB, S.SLOT}, (S.u.w + 7) / 8, (S.u.h + 7) / 8);
    D.to({S.SLOT, S.MARK}, Access::CopySrc); D.to({S.MARK}, Access::CopyDst);
    D.cl->copyBuffer(S.SLOT, 0, S.MARK, 0, S.keys * 4);
    std::uint32_t w[12];
    D.params(w, 0, 0, 0, 0, 0, 0, std::uint32_t(S.keys), 0, 0.0f);
    const BufferHandle ub = D.uniform(w, 12);
    D.run("rt_scan_chunk", {ub, S.SLOT, S.SUMS}, std::uint32_t((chunks + 63) / 64));
    D.run("rt_scan_top", {ub, S.SUMS}, 1);
    run1(D, "gi_scan_add", {uni(D, S, 0, 0, S.keys), S.SLOT, S.SUMS}, S.keys);
    run1(D, "gi_keys", {uni(D, S, 0, 0, S.keys), S.MARK, S.SLOT, S.KEYS}, S.keys);
    D.to({S.SLOT}, Access::CopySrc); D.to({RB}, Access::CopyDst);
    D.cl->copyBuffer(S.SLOT, 0, RB, 0, S.keys * 4);
    std::vector<std::uint32_t> slot;
    if (!restart(D, dev) || !D.read(RB, std::size_t(S.keys), slot)) return false;
    r.slotsMatch = true;
    std::uint32_t next = 0;
    for (int c = 0; c < kCascades; ++c){
        const Cascade& C = S.cpu[std::size_t(c)];
        S.u.sbase[c] = next; S.u.abase[c] = std::uint32_t(S.accWords); S.u.rbase[c] = std::uint32_t(S.rmFloats);
        for (std::size_t k = 0; k < C.slotOfKey.size(); ++k){
            const std::uint32_t gs = slot[S.u.d[c][3] + k], want = C.slotOfKey[k];
            r.slotsMatch = r.slotsMatch && (want == kNone ? gs == kNone : gs == next + want);
        }
        S.slots[c] = r.slots[c] = std::uint32_t(C.keyOfSlot.size());
        next += S.slots[c];
        S.accWords += std::uint64_t(S.slots[c]) * C.bins * C.bins * 5;
        S.rmFloats += std::uint64_t(S.slots[c]) * C.bins * C.bins * 4;
    }
    return true;
}
// One frame: trace, resolve, and after warm-up split.
void frame(Dev& D, const State& S, std::uint32_t f, bool split){
    const std::uint32_t gx = (S.u.rw + 7) / 8, gy = (S.u.rh + 7) / 8;
    D.run("gi_trace", {uni(D, S, f, 0, 0), S.GB, S.NI, S.NB, S.TA, S.CE, S.CS, S.RR}, gx, gy);
    run1(D, "gi_resolve", {uni(D, S, f, 0, S.cacheKeys), S.CS, S.CE}, S.cacheKeys);
    if (split) D.run("gi_split", {uni(D, S, f, 0, 0), S.GB, S.RR, S.CE, S.SLOT, S.ACC}, gx, gy);
}
// The merge from the coarsest cascade down, the harmonics and the gather.
void resolve(Dev& D, const State& S, std::uint32_t control){
    for (int c = kCascades - 1; c >= 0; --c){
        const std::uint64_t n = std::uint64_t(S.slots[c]) * S.u.nbin[c] * S.u.nbin[c];
        run1(D, "gi_merge", {uni(D, S, 0, std::uint32_t(c), n, control), S.ACC, S.SLOT, S.KEYS, S.RM}, n);
    }
    run1(D, "gi_sh", {uni(D, S, 0, 0, S.slots[0]), S.RM, S.SUB, S.SH}, S.slots[0]);
    D.run("gi_gather", {uni(D, S, 0, 0, 0), S.GB, S.SLOT, S.SH, S.OUT}, (S.u.w + 7) / 8, (S.u.h + 7) / 8);
}
double timeFrames(Dev& D, rhi::Device& dev, const State& S, const Params& p, std::uint32_t control, int repeats){
    std::vector<double> ms;
    for (int k = 0; k < repeats && D.err.empty(); ++k){
        frame(D, S, std::uint32_t(p.frames + k), true);
        resolve(D, S, control);
        const auto t0 = std::chrono::steady_clock::now();
        if (!dev.submitAndWait(D.err)) break;
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (!(D.cl = dev.begin(D.err))) break;
    }
    if (ms.empty()) return 0.0;
    std::sort(ms.begin(), ms.end());
    return ms[ms.size() / 2];
}
}  // namespace

GpuRun cascadesGpu(rhi::Device& dev, const Scene3& s, const GBuffer& g, const Params& p, bool control, int timeRepeats){
    GpuRun r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    State S;
    S.cpu = probesOf(s, g, p);
    const Grid cache = gridOf(s, p.cacheCell);
    S.u = base(s, g, p, S.cpu, cache);
    for (const Cascade& C : S.cpu) S.keys += C.grid.keys();
    S.cacheKeys = cache.keys();
    std::vector<PassInfo> table = passTable(gi_passes::kPasses), rt = passTable(rt_passes::kPasses);
    table.insert(table.end(), rt.begin(), rt.end());
    Dev D{dev, r.error, table};
    if (!(D.cl = dev.begin(r.error))) return r;
    upload(D, S, s, g);
    if (!allocate(D, dev, S, r)) return r;
    S.ACC = D.make(S.accWords * 4, kRW, "gi acc"); S.RM = D.make(S.rmFloats * 4, kRW, "gi merged");
    S.SH = D.make(std::uint64_t(S.slots[0]) * 28 * 4, kRW, "gi sh"); S.OUT = D.make(std::uint64_t(S.u.w) * S.u.h * 16, kRW, "gi out");
    S.CE = D.make(S.cacheKeys * 16, kRW, "gi cache"); S.CS = D.make(S.cacheKeys * 16, kRW, "gi cache sums");
    S.RR = D.make(std::uint64_t(S.u.rw) * S.u.rh * 32, kRW, "gi rays");
    clear(D, S, S.ACC, S.accWords); clear(D, S, S.CE, S.cacheKeys * 4); clear(D, S, S.CS, S.cacheKeys * 4);
    for (int f = 0; f < p.frames && restart(D, dev); ++f) frame(D, S, std::uint32_t(f), f >= p.warmup);
    resolve(D, S, control ? 1u : 0u);
    const std::uint64_t npx = std::uint64_t(S.u.w) * S.u.h;
    const BufferHandle RB = D.make(npx * 16, kRB, "gi out readback");
    D.to({S.OUT}, Access::CopySrc); D.to({RB}, Access::CopyDst);
    D.cl->copyBuffer(S.OUT, 0, RB, 0, npx * 16);
    std::vector<float> out;
    if (!restart(D, dev) || !D.read(RB, std::size_t(npx) * 4, out)) return r;
    r.indirect.resize(std::size_t(npx));
    for (std::size_t i = 0; i < r.indirect.size(); ++i) r.indirect[i] = {out[i * 4], out[i * 4 + 1], out[i * 4 + 2]};
    if (timeRepeats > 0){ r.frameMs = timeFrames(D, dev, S, p, control ? 1u : 0u, timeRepeats); r.repeats = timeRepeats; }
    if (D.err.empty()) dev.submitAndWait(D.err);
    return r;
}

}  // namespace raw::gi
