// GPU parity of the lighting: see raw/renderer/lighting_parity.hpp.
#include "raw/renderer/lighting_parity.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/renderer/lighting_scenes.hpp"
#include "raw/renderer/pbr_parity.hpp"
#include "shader_library.hpp"
#include "shaders/light_layout.hpp"
#include "shaders/pbr_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
namespace raw::gpu_check {
namespace {
using namespace lighting;
using rhi::Access;
using rhi::BufferHandle;
using rhi::BufferUsage;

struct LightParamsGpu { std::uint32_t nlights, nclusters, size, level, samples, levels, pad0, pad1; float grid[4]; };
struct PbrParamsGpu { std::uint32_t count, flags, nlights, envSize, envLevels, shOff, pad0, pad1; float grid[4], misc[4]; };

// Buffers made for one check and destroyed with it.
struct Gpu {
    rhi::Device& dev;
    std::string err;
    std::vector<BufferHandle> owned;
    ~Gpu() { for (BufferHandle b : owned) dev.destroyBuffer(b); }
    BufferHandle make(std::uint64_t bytes, BufferUsage u, const char* label) {
        const BufferHandle b = dev.createBuffer({std::max<std::uint64_t>(bytes, 16), u, label}, err);
        if (b.valid()) owned.push_back(b);
        return b;
    }
    BufferHandle upload(const void* data, std::uint64_t bytes, BufferUsage u, const char* label, std::vector<std::function<void(rhi::CommandList&)>>& pre) {
        const BufferHandle b = make(bytes, u | BufferUsage::CopyDst, label);
        std::vector<std::uint8_t> copy(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + bytes);
        pre.push_back([b, copy](rhi::CommandList& c) { c.upload(b, 0, copy.data(), copy.size()); });
        return b;
    }
    // Records `body` after the uploads, submits, and reads `out` back (bytes) into `dst`.
    bool run(std::vector<std::function<void(rhi::CommandList&)>>& pre, const std::function<void(rhi::CommandList&)>& body,
             BufferHandle out, std::uint64_t bytes, void* dst) {
        if (!err.empty()) return false;
        const BufferHandle rb = make(bytes, BufferUsage::MapRead | BufferUsage::CopyDst, "readback");
        rhi::CommandList* c = err.empty() ? dev.begin(err) : nullptr;
        if (!c) return false;
        for (auto& f : pre) f(*c);
        pre.clear();
        body(*c);
        const rhi::BufferBarrier bb[2] = {{out, Access::StorageWrite, Access::CopySrc}, {rb, Access::Undefined, Access::CopyDst}};
        c->barrier(bb);
        c->copyBuffer(out, 0, rb, 0, bytes);
        if (!dev.submitAndWait(err)) return false;
        const void* p = dev.mapRead(rb, bytes, err);
        if (!p) return false;
        std::memcpy(dst, p, bytes);
        dev.unmap(rb);
        return true;
    }
};
rhi::PipelineHandle pipeline(rhi::Device& dev, const char* name, const rhi::Binding* binds, int count, std::string& err) {
    const rhi::ShaderCode code = gpu_shaders::find(name, dev.shaderFormat());
    if (!code.bytes) { err = std::string("this build carries no ") + name + " shader"; return {}; }
    return dev.createComputePipeline({name, code, std::span(binds, std::size_t(count))}, err);
}
// The three lighting pipelines: clusters, prefilter, shading.
bool pipelines(rhi::Device& dev, rhi::PipelineHandle& pc, rhi::PipelineHandle& pp, rhi::PipelineHandle& ps, std::string& err) {
    const auto& c = light_passes::kPasses[0]; const auto& p = light_passes::kPasses[1]; const auto& s = pbr_passes::kPasses[1];
    pc = pipeline(dev, c.name, c.binds, c.bindCount, err);
    pp = pc.valid() ? pipeline(dev, p.name, p.binds, p.bindCount, err) : rhi::PipelineHandle{};
    ps = pp.valid() ? pipeline(dev, s.name, s.binds, s.bindCount, err) : rhi::PipelineHandle{};
    return ps.valid();
}
double ratio(double g, double r, double relB, double absB) { return std::fabs(g - r) / (relB * std::fabs(r) + absB); }
LightParamsGpu lightParams(const ClusterGrid& g, std::uint32_t nlights) {
    return {nlights, std::uint32_t(g.count()), 32, 0, kPrefilterSamples, 6, 0, 0,
            {float(std::tan(0.5 * g.fovy)), float(g.aspect), float(g.nearZ), float(g.farZ)}};
}

// The GPU prefilter of `env` (levels 1..5 from level 0) with `samples` samples, read back whole.
bool gpuPrefilter(rhi::Device& dev, rhi::PipelineHandle pipe, const std::vector<float>& env, std::uint32_t samples, std::vector<float>& out, std::string& err) {
    Gpu g{dev, {}, {}};
    std::vector<std::function<void(rhi::CommandList&)>> pre;
    const BufferHandle e = g.upload(env.data(), env.size() * 4, BufferUsage::Storage | BufferUsage::CopySrc, "env", pre);
    std::vector<BufferHandle> ubs, sbs;
    for (std::uint32_t lv = 1; lv < 6; ++lv) {
        LightParamsGpu p = lightParams(ClusterGrid{}, 0);
        p.level = lv; p.samples = samples;
        ubs.push_back(g.upload(&p, sizeof p, BufferUsage::Uniform, "prefilter params", pre));
        // The level's half-vector samples, as cube::prefilterTexel draws them, in double.
        const double a = pbr::alphaOf(lv / 5.0);
        std::vector<float> sv(std::size_t(samples) * 3);
        for (std::uint32_t k = 0; k < samples; ++k) {
            const double u1 = (k + 0.5) / samples, u2 = cube::radicalInverse(k);
            const double ct = std::sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1)), st = std::sqrt(std::max(0.0, 1.0 - ct * ct)), ph = 2.0 * pbr::kPi * u2;
            sv[k * 3] = float(ct); sv[k * 3 + 1] = float(st * std::cos(ph)); sv[k * 3 + 2] = float(st * std::sin(ph));
        }
        sbs.push_back(g.upload(sv.data(), sv.size() * 4, BufferUsage::Storage, "prefilter samples", pre));
    }
    out.assign(env.size(), 0.0f);
    const bool ok = g.run(pre, [&](rhi::CommandList& c) {
        for (std::uint32_t lv = 1; lv < 6; ++lv) {
            const rhi::BufferBarrier in[3] = {{ubs[lv - 1], Access::CopyDst, Access::Uniform},
                                              {e, lv == 1 ? Access::CopyDst : Access::StorageWrite, Access::StorageWrite},
                                              {sbs[lv - 1], Access::CopyDst, Access::StorageRead}};
            c.barrier(in);
            const BufferHandle binds[3] = {ubs[lv - 1], e, sbs[lv - 1]};
            const std::uint32_t n = std::uint32_t(cube::levelSize(32, int(lv)));
            c.dispatch(pipe, binds, (6 * n * n + 63) / 64, 1, 1);
        }
    }, e, env.size() * 4, out.data());
    err = g.err;
    return ok;
}
// The GPU cluster lists of one light set: counts, then 256 slots a cluster.
bool gpuClusters(rhi::Device& dev, rhi::PipelineHandle pipe, const std::vector<Light>& ls, std::vector<std::uint32_t>& k, std::string& err,
                 const ClusterGrid& grid = ClusterGrid{}) {
    Gpu g{dev, {}, {}};
    std::vector<std::function<void(rhi::CommandList&)>> pre;
    const LightParamsGpu p = lightParams(grid, std::uint32_t(ls.size()));
    const std::vector<float> lv = packLights(ls);
    const BufferHandle ub = g.upload(&p, sizeof p, BufferUsage::Uniform, "cluster params", pre);
    const BufferHandle lb = g.upload(lv.data(), lv.size() * 4, BufferUsage::Storage, "lights", pre);
    const std::uint64_t kn = std::uint64_t(grid.count()) * (1 + ClusterGrid::kMaxPerCluster);
    const BufferHandle kb = g.make(kn * 4, BufferUsage::Storage | BufferUsage::CopySrc, "clusters");
    k.assign(kn, 0u);
    const bool ok = g.run(pre, [&](rhi::CommandList& c) {
        const rhi::BufferBarrier in[3] = {{ub, Access::CopyDst, Access::Uniform}, {lb, Access::CopyDst, Access::StorageRead}, {kb, Access::Undefined, Access::StorageWrite}};
        c.barrier(in);
        const BufferHandle binds[3] = {ub, lb, kb};
        c.dispatch(pipe, binds, (std::uint32_t(grid.count()) + 63) / 64, 1, 1);
    }, kb, kn * 4, k.data());
    err = g.err;
    return ok;
}
std::vector<int> gpuList(const std::vector<std::uint32_t>& k, int c) {
    const ClusterGrid g;
    std::vector<int> v;
    for (std::uint32_t j = 0; j < k[std::size_t(c)]; ++j) v.push_back(int(k[std::size_t(g.count()) + std::size_t(c) * ClusterGrid::kMaxPerCluster + j]));
    return v;
}
// Distance from a light's centre to a cluster's box, against its range: near 0 is a boundary case.
double boundaryGap(const ClusterGrid& g, const Light& l, int c) {
    D3 lo, hi; g.bounds(c, lo, hi);
    const auto ax = [](double v, double a, double b) { return v < a ? a - v : v > b ? v - b : 0.0; };
    const double dx = ax(l.position.x, lo.x, hi.x), dy = ax(l.position.y, lo.y, hi.y), dz = ax(l.position.z, lo.z, hi.z);
    return std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - l.range) / std::max(1.0, l.range);
}
// A point inside the frustum, half of them within a random light's range.
D3 testPoint(scenes::Rng& q, const ClusterGrid& g, const std::vector<Light>& ls, int k) {
    D3 p = scenes::inFrustum(q, g, 0.2, 80.0);
    if (k % 2) {
        const Light& l = ls[2 + std::size_t(q.next() * double(ls.size() - 2))];
        const D3 o = scenes::randomDir(q);
        const double rr = l.range * q.next();
        p = {l.position.x + o.x * rr, l.position.y + o.y * rr, l.position.z + o.z * rr};
    }
    return p;
}
void clusterChecks(rhi::Device& dev, rhi::PipelineHandle pipe, LightingParity& r) {
    const ClusterGrid g;
    for (int sc = 0; sc < 8 && r.error.empty(); ++sc) {
        scenes::Rng rng{1000ull + std::uint64_t(sc)};
        const std::vector<Light> ls = scenes::scene(rng, g, 256);
        const auto cpu = assignClusters(g, ls);
        std::vector<std::uint32_t> k;
        if (!gpuClusters(dev, pipe, ls, k, r.error)) return;
        for (int c = 0; c < g.count(); ++c) {
            const std::vector<int> a = cpu[std::size_t(c)], b = gpuList(k, c);
            std::vector<int> diff;
            std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(diff));
            for (int li : diff) (boundaryGap(g, ls[std::size_t(li)], c) <= 1e-4 ? r.clusterBoundaryDiffs : r.clusterDiffs)++;
        }
        for (int p = 0; p < 8192; ++p) {
            scenes::Rng q{std::uint64_t(sc) * 100003ull + std::uint64_t(p) + 17};
            const D3 pt = testPoint(q, g, ls, p);
            const int c = g.clusterOf(pt);
            if (c < 0) continue;
            ++r.missPoints;
            const std::vector<int> b = gpuList(k, c);
            for (std::size_t i = 0; i < ls.size(); ++i) {
                D3 L; const Rgb e = illuminance(ls[i], pt, L);
                if ((e.r != 0.0 || e.g != 0.0 || e.b != 0.0) && std::find(b.begin(), b.end(), int(i)) == b.end()) ++r.gpuMisses;
            }
        }
    }
}
// Shading on the GPU from the given cluster buffer and environment buffer.
bool gpuShade(rhi::Device& dev, rhi::PipelineHandle pipe, const PbrParamsGpu& p, const std::vector<float>& samples, const std::vector<float>& tables,
              const std::vector<float>& lights, const std::vector<std::uint32_t>& k, const std::vector<float>& env, std::vector<float>& out, std::string& err) {
    Gpu g{dev, {}, {}};
    std::vector<std::function<void(rhi::CommandList&)>> pre;
    const BufferHandle b[6] = {g.upload(&p, sizeof p, BufferUsage::Uniform, "shade params", pre),
                               g.upload(samples.data(), samples.size() * 4, BufferUsage::Storage, "samples", pre),
                               g.upload(tables.data(), tables.size() * 4, BufferUsage::Storage, "tables", pre),
                               g.upload(lights.data(), lights.size() * 4, BufferUsage::Storage, "lights", pre),
                               g.upload(k.data(), k.size() * 4, BufferUsage::Storage, "clusters", pre),
                               g.upload(env.data(), env.size() * 4, BufferUsage::Storage, "env", pre)};
    const BufferHandle o = g.make(std::uint64_t(p.count) * 12, BufferUsage::Storage | BufferUsage::CopySrc, "shaded");
    out.assign(std::size_t(p.count) * 3, 0.0f);
    const bool ok = g.run(pre, [&](rhi::CommandList& c) {
        rhi::BufferBarrier in[7] = {{b[0], Access::CopyDst, Access::Uniform}};
        for (int i = 1; i < 6; ++i) in[i] = {b[i], Access::CopyDst, Access::StorageRead};
        in[6] = {o, Access::Undefined, Access::StorageWrite};
        c.barrier(in);
        const BufferHandle binds[7] = {b[0], b[1], b[2], b[3], b[4], b[5], o};
        c.dispatch(pipe, binds, (p.count + 63) / 64, 1, 1);
    }, o, std::uint64_t(p.count) * 12, out.data());
    err = g.err;
    return ok;
}
}  // namespace

LightingParity lightingParity(rhi::Device& dev) {
    LightingParity r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    rhi::PipelineHandle pc, pp, ps;
    if (!pipelines(dev, pc, pp, ps, r.error)) return r;
    clusterChecks(dev, pc, r);
    if (!r.error.empty()) return r;
    // The environment: the CPU prefilter as reference, the GPU prefilter checked and then used.
    const Cube level0 = cubeFrom(proceduralSky, 32, 6);
    Cube ref = level0;
    prefilter(ref, kPrefilterSamples);
    const std::vector<Rgb> sh = shProject(level0);
    const std::vector<float> env0 = packEnvironment(level0, sh);
    std::vector<float> env, envHalf;
    if (!gpuPrefilter(dev, pp, env0, kPrefilterSamples, env, r.error) || !gpuPrefilter(dev, pp, env0, kPrefilterSamples / 2, envHalf, r.error)) return r;
    for (std::size_t i = ref.levelOffset(1); i < ref.rgb.size(); ++i) {
        r.prefilterWorstRatio = std::max(r.prefilterWorstRatio, ratio(env[i], ref.rgb[i], 1e-3, 1e-6));
        r.prefilterControlRatio = std::max(r.prefilterControlRatio, ratio(envHalf[i], ref.rgb[i], 1e-3, 1e-6));
    }
    // Shading.
    const ClusterGrid g;
    const pbr::Tables tables;
    scenes::Rng rng{kLightingSeed};
    const std::vector<Light> ls = scenes::scene(rng, g, 256);
    const auto cpu = assignClusters(g, ls);
    std::vector<std::uint32_t> k;
    if (!gpuClusters(dev, pc, ls, k, r.error)) return r;
    std::vector<Sample> ss(kLightingSamples);
    std::vector<float> packed(std::size_t(kLightingSamples) * kSampleFloats);
    for (std::uint32_t i = 0; i < kLightingSamples; ++i) {
        scenes::Rng q{kLightingSeed * 31ull + i};
        ss[i] = scenes::sampleAt(q, testPoint(q, g, ls, int(i)), int(i));
        packSample(ss[i], packed.data() + std::size_t(i) * kSampleFloats);
    }
    const double ex = exposure(kLightingEv100);
    PbrParamsGpu p{kLightingSamples, 0, std::uint32_t(ls.size()), 32, 6, std::uint32_t(level0.rgb.size()), 0, 0,
                   {float(std::tan(0.5 * g.fovy)), float(g.aspect), float(g.nearZ), float(g.farZ)}, {float(ex), 0, 0, 0}};
    const std::vector<float> lights = packLights(ls), tab = packTables(tables);
    std::vector<std::uint32_t> dropped(k.size(), 0u);           // the control: every third listed light removed
    for (int c = 0; c < g.count(); ++c) {
        std::uint32_t n = 0;
        for (int li : cpu[std::size_t(c)]) if (li % 3 != 1) dropped[std::size_t(g.count()) + std::size_t(c) * ClusterGrid::kMaxPerCluster + n++] = std::uint32_t(li);
        dropped[std::size_t(c)] = n;
    }
    std::vector<float> out, ctl;
    if (!gpuShade(dev, ps, p, packed, tab, lights, k, env, out, r.error) || !gpuShade(dev, ps, p, packed, tab, lights, dropped, env, ctl, r.error)) return r;
    for (std::uint32_t i = 0; i < kLightingSamples; ++i) {
        const Sample& s = ss[i];
        const int c = g.clusterOf(s.p);
        const Rgb a = c >= 0 ? shadePunctual(s, tables, ls, cpu[std::size_t(c)]) : pbr::emission(s.m), b = shadeIbl(s, tables, ref, sh);
        const double rv[3] = {(a.r + b.r) * ex, (a.g + b.g) * ex, (a.b + b.b) * ex};
        for (int j = 0; j < 3; ++j) {
            const double gv = out[std::size_t(i) * 3 + j], q = ratio(gv, rv[j], 2e-3, 1e-5);
            if (!(q <= 1.0)) ++r.shadeOutside;
            if (!(q <= r.shadeWorstRatio)) { r.shadeWorstRatio = std::isnan(q) ? 1e30 : q; r.shadeWorstAbs = std::fabs(gv - rv[j]); r.shadeWorstSample = int(i); }
            r.shadeControlRatio = std::max(r.shadeControlRatio, ratio(ctl[std::size_t(i) * 3 + j], rv[j], 2e-3, 1e-5));
        }
    }
    r.samples = kLightingSamples;
    return r;
}
bool shadeOnGpu(rhi::Device& dev, const ClusterGrid& grid, const std::vector<Sample>& samples, const std::vector<Light>& lights,
                const Cube& env, const std::vector<Rgb>& sh, double exposure, std::vector<float>& out, std::string& err) {
    rhi::PipelineHandle pc, pp, ps;
    if (!pipelines(dev, pc, pp, ps, err)) return false;
    std::vector<float> envGpu;
    std::vector<std::uint32_t> k;
    if (!gpuPrefilter(dev, pp, packEnvironment(env, sh), kPrefilterSamples, envGpu, err) || !gpuClusters(dev, pc, lights, k, err, grid)) return false;
    const pbr::Tables tables;
    const std::vector<float> tab = packTables(tables), lv = packLights(lights);
    out.assign(samples.size() * 3, 0.0f);
    constexpr std::size_t kChunk = 1u << 16;
    for (std::size_t first = 0; first < samples.size(); first += kChunk) {
        const std::size_t n = std::min(kChunk, samples.size() - first);
        std::vector<float> packed(n * kSampleFloats), part;
        for (std::size_t i = 0; i < n; ++i) { Sample s = samples[first + i]; packSample(s, packed.data() + i * kSampleFloats); }
        const PbrParamsGpu p{std::uint32_t(n), 0, std::uint32_t(lights.size()), std::uint32_t(env.size), std::uint32_t(env.levels),
                             std::uint32_t(env.rgb.size()), 0, 0,
                             {float(std::tan(0.5 * grid.fovy)), float(grid.aspect), float(grid.nearZ), float(grid.farZ)}, {float(exposure), 0, 0, 0}};
        if (!gpuShade(dev, ps, p, packed, tab, lv, k, envGpu, part, err)) return false;
        std::copy(part.begin(), part.end(), out.begin() + std::ptrdiff_t(first * 3));
    }
    return true;
}

}  // namespace raw::gpu_check
