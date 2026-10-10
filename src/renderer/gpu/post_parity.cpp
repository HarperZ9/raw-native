// GPU parity of GTAO, SSR and the TAA resolve: see raw/renderer/post_parity.hpp.
#include "raw/renderer/post_parity.hpp"
#include "raw/renderer/post.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/taa.hpp"
#include "raw/tools/model_scene.hpp"
#include "gpu_util.hpp"
#include "shaders/post_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace raw::gpu_check {
namespace {
using namespace util;
using rhi::Access;
using rhi::BufferUsage;

struct PostParamsGpu { uint32_t width, height, slices, steps, refine, flags, ssrSteps, pad1; float view[4], ssr[4], taa[4]; };
static_assert(sizeof(PostParamsGpu) == 80, "PostParams layout");

PostParamsGpu params(int w, int h, double tanHalf, double aspect, double nearZ, uint32_t flags) {
    const post::GtaoParams g;
    const post::SsrParams s;
    return {uint32_t(w), uint32_t(h), uint32_t(g.slices), uint32_t(g.steps), uint32_t(s.refine), flags, uint32_t(s.steps), 0,
            {float(tanHalf), float(aspect), float(nearZ), float(g.radius)}, {float(s.maxDistance), float(s.thickness), 0, 0},
            {taa::kBlend, taa::kClipSigma, 0, 0}};
}
std::vector<float> pack(const post::ViewGBuffer& v) {
    std::vector<float> g(std::size_t(v.w) * v.h * 8, 0.0f);
    for (std::size_t i = 0; i < std::size_t(v.w) * v.h; ++i) {
        for (int k = 0; k < 3; ++k) { g[i * 8 + std::size_t(k)] = v.pos[i * 3 + std::size_t(k)]; g[i * 8 + 4 + std::size_t(k)] = v.nrm[i * 3 + std::size_t(k)]; }
        g[i * 8 + 3] = v.depth[i];
    }
    return g;
}
// One pass over the view G-buffer (post_gtao: index 0, post_ssr: index 1); one float a pixel.
bool runView(rhi::Device& dev, int pass, const post::ViewGBuffer& v, std::vector<float>& out, std::string& err) {
    Gpu gpu(dev);
    const auto& L = post_passes::kPasses[pass];
    const rhi::PipelineHandle pipe = compute(dev, L.name, L.binds, L.bindCount, gpu.err);
    const PostParamsGpu p = params(v.w, v.h, v.tanHalf, v.aspect, v.nearZ, 0);
    const std::vector<float> g = pack(v);
    const uint32_t n = uint32_t(v.w * v.h);
    const rhi::BufferHandle ub = gpu.upload(&p, sizeof p, BufferUsage::Uniform, "post params");
    const rhi::BufferHandle gb = gpu.upload(g.data(), g.size() * 4, BufferUsage::Storage, "view gbuffer");
    const rhi::BufferHandle ob = gpu.make(uint64_t(n) * 4, BufferUsage::Storage | BufferUsage::CopySrc, "post out");
    out.assign(n, 0.0f);
    const bool ok = pipe.valid() && runBuffer(gpu, [&](rhi::CommandList& c) {
        const rhi::BufferBarrier in[3] = {{ub, Access::CopyDst, Access::Uniform}, {gb, Access::CopyDst, Access::StorageRead}, {ob, Access::Undefined, Access::StorageWrite}};
        c.barrier(in);
        const rhi::BufferHandle binds[3] = {ub, gb, ob};
        c.dispatch(pipe, binds, (n + 63) / 64, 1, 1);
    }, ob, uint64_t(n) * 4, out.data());
    if (!ok) err = gpu.err;
    return ok;
}
// One TAA resolve on the GPU.
bool runTaa(rhi::Device& dev, const taa::Frame& f, const std::vector<float>& hist, const std::vector<float>& prevDepth, uint32_t flags,
            std::vector<float>& out, std::string& err) {
    Gpu gpu(dev);
    const auto& L = post_passes::kPasses[2];
    const rhi::PipelineHandle pipe = compute(dev, L.name, L.binds, L.bindCount, gpu.err);
    const uint32_t n = uint32_t(f.w * f.h);
    const PostParamsGpu p = params(f.w, f.h, 0, 1, 0.1, flags | (hist.empty() ? 4u : 0u));
    std::vector<float> cur(std::size_t(n) * 4);
    for (std::size_t i = 0; i < n; ++i) { for (int k = 0; k < 3; ++k) cur[i * 4 + std::size_t(k)] = f.rgb[i * 3 + std::size_t(k)]; cur[i * 4 + 3] = f.depth[i]; }
    const std::vector<float> h = hist.empty() ? std::vector<float>(std::size_t(n) * 3, 0.0f) : hist;
    const std::vector<float> d = prevDepth.empty() ? std::vector<float>(n, -1.0f) : prevDepth;
    const rhi::BufferHandle ub = gpu.upload(&p, sizeof p, BufferUsage::Uniform, "taa params");
    const rhi::BufferHandle cb = gpu.upload(cur.data(), cur.size() * 4, BufferUsage::Storage, "taa current");
    const rhi::BufferHandle rb = gpu.upload(f.prev.data(), f.prev.size() * 4, BufferUsage::Storage, "taa reprojection");
    const rhi::BufferHandle db = gpu.upload(d.data(), d.size() * 4, BufferUsage::Storage, "taa previous depth");
    const rhi::BufferHandle hb = gpu.upload(h.data(), h.size() * 4, BufferUsage::Storage, "taa history");
    const rhi::BufferHandle ob = gpu.make(uint64_t(n) * 12, BufferUsage::Storage | BufferUsage::CopySrc, "taa out");
    out.assign(std::size_t(n) * 3, 0.0f);
    const bool ok = pipe.valid() && runBuffer(gpu, [&](rhi::CommandList& c) {
        const rhi::BufferBarrier in[6] = {{ub, Access::CopyDst, Access::Uniform}, {cb, Access::CopyDst, Access::StorageRead}, {rb, Access::CopyDst, Access::StorageRead},
                                          {db, Access::CopyDst, Access::StorageRead}, {hb, Access::CopyDst, Access::StorageRead}, {ob, Access::Undefined, Access::StorageWrite}};
        c.barrier(in);
        const rhi::BufferHandle binds[6] = {ub, cb, rb, db, hb, ob};
        c.dispatch(pipe, binds, (n + 63) / 64, 1, 1);
    }, ob, uint64_t(n) * 12, out.data());
    if (!ok) err = gpu.err;
    return ok;
}
Scene movedLast(const Scene& s) {
    Scene m = s;
    float lo = 1e30f, hi = -1e30f;
    for (const Mesh& me : s.meshes) for (const Vec3& p : me.positions) { lo = std::min({lo, p.x, p.y, p.z}); hi = std::max({hi, p.x, p.y, p.z}); }
    for (Vec3& p : m.meshes.back().positions) p.x += 0.01f * (hi - lo);
    return m;
}
post::ViewGBuffer viewOf(const Scene& s, int size) {
    RasterOptions ro; ro.perspectiveDepth = true;
    return post::viewGBuffer(rasterize(s, size, size, nullptr, nullptr, ro), s.camera);
}
}  // namespace

bool PostParity::pass() const {
    bool ok = error.empty() && !scenes.empty() && taaFrames > 0 && double(taaWorstOutside) <= 1e-3 * double(taaPixels) &&
              gtaoControlFails && ssrControlFails && taaControlFails;
    for (const PostSceneCase& c : scenes) ok = ok && c.pass();
    return ok;
}

PostParity postParity(rhi::Device& dev, const std::vector<std::pair<std::string, const Scene*>>& scenes, int size) {
    PostParity r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    for (std::size_t si = 0; si < scenes.size(); ++si) {
        const post::ViewGBuffer v = viewOf(*scenes[si].second, size);
        PostSceneCase c; c.scene = scenes[si].first;
        std::vector<float> ga, gs;
        if (!runView(dev, 0, v, ga, r.error) || !runView(dev, 1, v, gs, r.error)) return r;
        const std::vector<double> ca = post::gtao(v);
        const std::vector<int> cs = post::ssr(v);
        for (std::size_t i = 0; i < ca.size(); ++i) {
            if (v.depth[i] < 0.0f) continue;
            ++c.pixels;
            const double e = std::fabs(double(ga[i]) - ca[i]);
            c.gtaoWorst = std::max(c.gtaoWorst, e);
            c.gtaoOutside += e > 1e-3;
            const int gh = int(std::lround(gs[i]));
            c.ssrDiffs += gh != cs[i];
            if (gh != cs[i]) {   // reported: what kind of difference
                if (gh < 0 || cs[i] < 0) {
                    ++c.ssrHitVsMiss;
                    if (std::getenv("RAW_NATIVE_POST_DUMP") && c.ssrHitVsMiss <= 12)   // diagnosis
                        std::fprintf(stderr, "%s px %zu %zu gpu %d cpu %d\n", c.scene.c_str(), i % std::size_t(size), i / std::size_t(size), gh, cs[i]);
                }
                else if (std::abs(gh % size - cs[i] % size) <= 1 && std::abs(gh / size - cs[i] / size) <= 1) ++c.ssrNeighbour;
            }
        }
        if (si == 0) {   // controls: the GPU passes on the scene with its last mesh moved
            const post::ViewGBuffer m = viewOf(movedLast(*scenes[si].second), size);
            std::vector<float> ma, ms;
            if (!runView(dev, 0, m, ma, r.error) || !runView(dev, 1, m, ms, r.error)) return r;
            long ao = 0, sr = 0;
            for (std::size_t i = 0; i < ca.size(); ++i) {
                if (v.depth[i] < 0.0f) continue;
                ao += std::fabs(double(ma[i]) - ca[i]) > 1e-3;
                sr += int(std::lround(ms[i])) != cs[i];
            }
            r.gtaoControlFails = double(ao) > 1e-3 * double(c.pixels);
            r.ssrControlFails = double(sr) > 1e-3 * double(c.pixels);
        }
        r.scenes.push_back(c);
    }
    // TAA: the moving-box sequence, CPU and GPU each feeding back its own history.
    CliParams p; p.width = p.height = 128;
    const Scene base = sceneFromParams(p, nullptr);
    const Mat4 vp = mul(base.camera.proj(), base.camera.view());
    const auto sx = [&](Vec3 q) { const Vec4 c = mul(vp, Vec4{q.x, q.y, q.z, 1}); return (c.x / c.w * 0.5f + 0.5f) * 128.0f; };
    const float dx = 1.5f * 0.01f / (sx({0.01f, 1, 0}) - sx({0, 1, 0}));
    std::vector<float> hc, hg, pd;
    long ctlOff = 0;
    r.taaPixels = 128 * 128;
    for (int k = 0; k < 32; ++k) {
        Scene s = base;
        for (Vec3& q : s.meshes.back().positions) q.x += dx * float(k);
        float jx, jy;
        taa::jitter(k, jx, jy);
        const taa::Frame f = taa::render(s, s.camera, k ? Vec3{dx, 0, 0} : Vec3{}, 128, 128, jx, jy);
        std::vector<float> og;
        if (!runTaa(dev, f, hg, pd, 3u, og, r.error)) return r;
        if (k == 8) {   // control: the GPU history shifted one pixel
            std::vector<float> shifted(hg.size());
            for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
                for (int c = 0; c < 3; ++c) shifted[(std::size_t(y) * 128 + x) * 3 + std::size_t(c)] = hg[(std::size_t(y) * 128 + std::min(127, x + 1)) * 3 + std::size_t(c)];
            std::vector<float> oc;
            if (!runTaa(dev, f, shifted, pd, 3u, oc, r.error)) return r;
            const std::vector<float> cc = taa::resolve(f, hc, pd);
            for (std::size_t i = 0; i < std::size_t(r.taaPixels); ++i)
                for (int c = 0; c < 3; ++c) if (std::fabs(double(oc[i * 3 + std::size_t(c)]) - cc[i * 3 + std::size_t(c)]) > 1e-3) { ++ctlOff; break; }
        }
        hc = taa::resolve(f, hc, pd);
        long off = 0;
        for (std::size_t i = 0; i < std::size_t(r.taaPixels); ++i) {
            bool bad = false;
            for (int c = 0; c < 3; ++c) {
                const double e = std::fabs(double(og[i * 3 + std::size_t(c)]) - hc[i * 3 + std::size_t(c)]);
                r.taaWorst = std::max(r.taaWorst, e);
                bad = bad || e > 1e-3;
            }
            off += bad;
        }
        r.taaWorstOutside = std::max(r.taaWorstOutside, off);
        {   // reported, not bounded: the same step on the GPU's own history (method note 8)
            const std::vector<float> one = taa::resolve(f, hg, pd);
            long step = 0;
            for (std::size_t i = 0; i < std::size_t(r.taaPixels); ++i) {
                bool bad = false;
                for (int c = 0; c < 3; ++c) bad = bad || std::fabs(double(og[i * 3 + std::size_t(c)]) - one[i * 3 + std::size_t(c)]) > 1e-3;
                step += bad;
            }
            r.taaWorstOneStep = std::max(r.taaWorstOneStep, step);
        }
        hg = og; pd = f.depth;
        ++r.taaFrames;
    }
    r.taaControlFails = double(ctlOff) > 1e-3 * double(r.taaPixels);
    return r;
}

std::string PostParity::json() const {
    std::string e;
    for (char ch : error) { if (ch == '"' || ch == '\\') e += '\\'; e += ch == '\n' ? ' ' : ch; }
    std::string s = "{\n \"check\": \"m3 post gpu parity\",\n \"backend\": \"" + backend + "\",\n \"adapter\": \"" + adapter + "\",\n \"error\": \"" + e + "\",\n \"scenes\": [\n";
    for (std::size_t k = 0; k < scenes.size(); ++k) {
        const PostSceneCase& c = scenes[k];
        char b[512];
        std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"pixels\": %ld, \"gtao_outside_1e-3\": %ld, \"gtao_worst\": %.3e, \"ssr_diffs\": %ld, \"ssr_hit_vs_miss\": %ld, \"ssr_neighbour\": %ld, \"pass\": %s}%s\n",
                      c.scene.c_str(), c.pixels, c.gtaoOutside, c.gtaoWorst, c.ssrDiffs, c.ssrHitVsMiss, c.ssrNeighbour, c.pass() ? "true" : "false", k + 1 < scenes.size() ? "," : "");
        s += b;
    }
    char b[512];
    std::snprintf(b, sizeof b, " ],\n \"taa\": {\"frames\": %d, \"pixels\": %ld, \"worst_frame_outside_1e-3\": %ld, \"worst\": %.3e, \"reported_one_step_worst_frame\": %ld},\n"
                  " \"controls_fail\": {\"gtao\": %s, \"ssr\": %s, \"taa\": %s},\n \"pass\": %s\n}\n",
                  taaFrames, taaPixels, taaWorstOutside, taaWorst, taaWorstOneStep, gtaoControlFails ? "true" : "false", ssrControlFails ? "true" : "false",
                  taaControlFails ? "true" : "false", pass() ? "true" : "false");
    return s + b;
}
}  // namespace raw::gpu_check
