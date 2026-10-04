// Threads on the RHI: the passes of shaders/threads.wgsl as a frame graph per
// frame, the same order the web host records (web/threads.mjs).
#include "raw/renderer/threads.hpp"
#include "raw/graph/frame_graph.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/shaders/threads_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <span>
namespace raw::threads {
namespace {
using rhi::Access;
using rhi::BufferHandle;
using rhi::BufferUsage;
// The WGSL Params struct, field for field (144 bytes, the same in a cbuffer).
struct Params {
    float t, dt, fr, aspect, u, ta, tb, spacing, nlev, gain, scale, keep, expo, spec, dur, fxs;
    float pr0[4], pr1[4];
    uint32_t np; int32_t wa, wb, ch;
    uint32_t w, h, substeps, simw, simh, seedbase; float simt; uint32_t pad0;
};
static_assert(sizeof(Params) == 144);
struct LevelParams { uint32_t w, h, level; float inv; };
constexpr float kExpo[kWorlds] = {0.6f, 0.5f, 0.45f, 0.5f, 0.55f, 0.5f, 0.6f, 0.5f, 0.55f, 0.5f, 0.5f, 0.55f, 0.6f, 0.55f, 0.45f};
constexpr float kProbs[6] = {0.429524f, 0.192914f, 0.155632f, 0.097257f, 0.075574f, 0.038923f};
constexpr uint32_t kSimW = 1024, kSimH = 576, kSimSteps = 10;
constexpr float kFixed = 4096.0f;
uint32_t div(uint32_t a, uint32_t b){ return (a + b - 1) / b; }
uint32_t levelDim(uint32_t n, uint32_t l){ return std::max(1u, n >> l); }

struct Pipes { rhi::PipelineHandle h[8]; };
bool makePipes(rhi::Device& dev, Pipes& p, std::string& err){
    int i = 0;
    for (const auto& pl : threads_passes::kPasses){
        rhi::ShaderCode code = gpu_shaders::find(pl.name, dev.shaderFormat());
        if (!code.bytes){ err = std::string("threads: this build carries no code for ") + pl.name; return false; }
        p.h[i] = dev.createComputePipeline({pl.name, code, std::span(pl.binds, (size_t)pl.bindCount)}, err);
        if (!p.h[i++].valid()) return false;
    }
    return true;
}
enum Pass { Init, RdSeed, Rd, Decay, Advance, PyrFirst, PyrNext, Finish };

struct State {
    rhi::Device& dev; const Settings& s; Pipes pipes;
    BufferHandle params, level[7], pos, acc, sim[2], pyr[2], frame, stage;
    uint32_t w, h, np;
    std::vector<BufferHandle> owned;
    BufferHandle make(uint64_t size, BufferUsage u, const char* label, std::string& err){
        BufferHandle b = dev.createBuffer({size, u, label}, err);
        if (b.valid()) owned.push_back(b);
        return b;
    }
    ~State(){ for (BufferHandle b : owned) dev.destroyBuffer(b); }
};
bool makeBuffers(State& st, std::string& err){
    const auto S = BufferUsage::Storage;
    const uint64_t px = (uint64_t)st.w * st.h;
    uint64_t a = 0, b = 0;
    for (uint32_t l = 1; l < 8; ++l) (l % 2 ? a : b) += (uint64_t)levelDim(st.w, l) * levelDim(st.h, l) * 16;
    st.params = st.make(sizeof(Params), BufferUsage::Uniform | BufferUsage::CopyDst, "threads params", err);
    for (int l = 0; l < 7; ++l) st.level[l] = st.make(sizeof(LevelParams), BufferUsage::Uniform | BufferUsage::CopyDst, "threads level", err);
    st.pos = st.make((uint64_t)st.np * 16, S, "threads particles", err);
    st.acc = st.make(px * 16, S, "threads accumulator", err);
    st.sim[0] = st.make((uint64_t)kSimW * kSimH * 8, S, "threads sim a", err);
    st.sim[1] = st.make((uint64_t)kSimW * kSimH * 8, S, "threads sim b", err);
    st.pyr[0] = st.make(a, S, "threads bloom odd", err);
    st.pyr[1] = st.make(b, S, "threads bloom even", err);
    st.frame = st.make(px * 4, S | BufferUsage::CopySrc, "threads frame", err);
    st.stage = st.make(px * 4, BufferUsage::MapRead | BufferUsage::CopyDst, "threads readback", err);
    return err.empty() && st.owned.size() == 16;   // params, 7 levels, pos, acc, 2 sims, 2 blooms, frame, stage
}
Params paramsFor(const State& st, int frameIndex, float simt){
    const Settings& s = st.s;
    const float dt = 1.0f / s.fps, sub = (float)std::max(1u, s.substeps);
    const float t = frameIndex * dt + 3.0f;   // start past the opening line
    Params p{};
    p.t = t; p.dt = dt / sub; p.fr = frameIndex * sub; p.aspect = (float)st.w / (float)st.h;
    p.ta = t; p.tb = t; p.spacing = s.spacing; p.nlev = s.levels;
    p.gain = (float)((1u << 21) / (double)st.np * ((double)st.w * st.h / (3840.0 * 2160.0)) * (dt * 30.0) / sub);
    p.scale = 1.0f; p.keep = std::pow(s.persistence, dt * 30.0f);
    p.expo = kExpo[s.world] * s.exposure; p.dur = 1e9f; p.fxs = kFixed;
    std::memcpy(p.pr0, kProbs, 16); p.pr1[0] = kProbs[4]; p.pr1[1] = kProbs[5];
    p.np = st.np; p.wa = p.wb = s.world; p.ch = 1;
    p.w = st.w; p.h = st.h; p.substeps = (uint32_t)sub; p.simw = kSimW; p.simh = kSimH; p.seedbase = 0x9e3779b9u; p.simt = simt;
    return p;
}
using graph::FrameGraph;
using graph::Resource;
// The setup submission: level parameters, particle seeds and, for Morphogen,
// the seeded field run 1,200 steps.
bool setup(State& st, std::string& err){
    FrameGraph g(&st.dev);
    const Params p = paramsFor(st, 0, 8.0f);
    Resource par = g.importBuffer("params", st.params), pos = g.importBuffer("pos", st.pos);
    g.addPass("params", {{par, Access::CopyDst}}, [&](graph::PassContext& c){ c.cmd->upload(st.params, 0, &p, sizeof p); });
    std::vector<LevelParams> lp;
    for (uint32_t l = 0; l < 7; ++l) lp.push_back({st.w, st.h, l + 1, 1.0f / kFixed});
    for (int l = 0; l < 7; ++l){
        Resource r = g.importBuffer("level", st.level[l]);
        g.addPass("level", {{r, Access::CopyDst}}, [&, l](graph::PassContext& c){ c.cmd->upload(st.level[l], 0, &lp[l], sizeof(LevelParams)); });
        g.markOutput(r);
    }
    g.addPass("init", {{par, Access::Uniform}, {pos, Access::StorageWrite}}, [&](graph::PassContext& c){
        BufferHandle b[] = {st.params, st.pos}; c.cmd->dispatch(st.pipes.h[Init], b, div(st.np, 256), 1, 1); });
    g.markOutput(pos);
    if (st.s.world == 3){
        Resource sim[2] = {g.importBuffer("simA", st.sim[0]), g.importBuffer("simB", st.sim[1])};
        g.addPass("rd seed", {{par, Access::Uniform}, {sim[0], Access::StorageWrite}}, [&](graph::PassContext& c){
            BufferHandle b[] = {st.params, st.sim[0]}; c.cmd->dispatch(st.pipes.h[RdSeed], b, div(kSimW, 16), div(kSimH, 16), 1); });
        for (int k = 0; k < 1200; ++k){
            const int s = k % 2;
            g.addPass("rd", {{par, Access::Uniform}, {sim[s], Access::StorageRead}, {sim[1 - s], Access::StorageWrite}}, [&, s](graph::PassContext& c){
                BufferHandle b[] = {st.params, st.sim[s], st.sim[1 - s]}; c.cmd->dispatch(st.pipes.h[Rd], b, div(kSimW, 16), div(kSimH, 16), 1); });
        }
        g.markOutput(sim[0]);
    }
    return g.execute(err);
}
// One frame: parameters, field steps, decay, advance, seven bloom levels, the
// finish and, on the last frame, the read-back.
bool frame(State& st, int i, bool last, std::string& err){
    const bool morph = st.s.world == 3;
    const Params p = paramsFor(st, i, 4.0f + (i / st.s.fps + 15.0f) * 0.8f);
    FrameGraph g(&st.dev);
    Resource par = g.importBuffer("params", st.params), pos = g.importBuffer("pos", st.pos), acc = g.importBuffer("acc", st.acc);
    Resource sim[2] = {g.importBuffer("simA", st.sim[0]), g.importBuffer("simB", st.sim[1])};
    Resource pyr[2] = {g.importBuffer("bloomOdd", st.pyr[0]), g.importBuffer("bloomEven", st.pyr[1])};
    Resource fr = g.importBuffer("frame", st.frame), stage = g.importBuffer("stage", st.stage);
    const uint32_t gx = div(st.w, 16), gy = div(st.h, 16);
    auto run = [&](Pass ps, std::vector<BufferHandle> b, uint32_t x, uint32_t y){
        return [&st, ps, b, x, y](graph::PassContext& c){ c.cmd->dispatch(st.pipes.h[ps], b, x, y, 1); }; };
    g.addPass("params", {{par, Access::CopyDst}}, [&](graph::PassContext& c){ c.cmd->upload(st.params, 0, &p, sizeof p); });
    for (uint32_t k = 0; morph && k < kSimSteps; ++k){
        const int s = k % 2;
        g.addPass("rd", {{par, Access::Uniform}, {sim[s], Access::StorageRead}, {sim[1 - s], Access::StorageWrite}},
                  run(Rd, {st.params, st.sim[s], st.sim[1 - s]}, div(kSimW, 16), div(kSimH, 16)));
    }
    g.addPass("decay", {{par, Access::Uniform}, {acc, Access::StorageWrite}}, run(Decay, {st.params, st.acc}, gx, gy));
    g.addPass("advance", {{par, Access::Uniform}, {pos, Access::StorageWrite}, {acc, Access::StorageWrite}, {sim[0], Access::StorageRead}},
              run(Advance, {st.params, st.pos, st.acc, st.sim[0]}, div(st.np, 256), 1));
    for (uint32_t l = 1; l < 8; ++l){
        Resource lv = g.importBuffer("level", st.level[l - 1]);
        Resource src = l == 1 ? acc : pyr[l % 2 ? 1 : 0], dst = pyr[l % 2 ? 0 : 1];
        BufferHandle sb = l == 1 ? st.acc : st.pyr[l % 2 ? 1 : 0], db = st.pyr[l % 2 ? 0 : 1];
        g.addPass("bloom", {{lv, Access::Uniform}, {src, Access::StorageRead}, {dst, Access::StorageWrite}},
                  run(l == 1 ? PyrFirst : PyrNext, {st.level[l - 1], sb, db}, div(levelDim(st.w, l), 16), div(levelDim(st.h, l), 16)));
    }
    g.addPass("finish", {{par, Access::Uniform}, {acc, Access::StorageRead}, {pyr[0], Access::StorageRead}, {pyr[1], Access::StorageRead}, {fr, Access::StorageWrite}},
              run(Finish, {st.params, st.acc, st.pyr[0], st.pyr[1], st.frame}, gx, gy));
    if (last){
        g.addPass("readback", {{fr, Access::CopySrc}, {stage, Access::CopyDst}},
                  [&](graph::PassContext& c){ c.cmd->copyBuffer(st.frame, 0, st.stage, 0, (uint64_t)st.w * st.h * 4); });
        g.markOutput(stage);
    } else {
        g.markOutput(fr);
    }
    return g.execute(err);
}
}

bool render(rhi::Device& dev, const Settings& s, Result& out, std::string& err){
    if (s.world < 0 || s.world >= kWorlds){ err = "threads: world must be 0 to 14"; return false; }
    if (s.width < 16 || s.height < 16 || s.frames < 1 || s.fps <= 0){ err = "threads: size, frames and fps must be positive"; return false; }
    State st{.dev = dev, .s = s, .w = (uint32_t)s.width, .h = (uint32_t)s.height,
             .np = std::clamp(s.particles, 1024u, 1u << 23)};
    if (!makePipes(dev, st.pipes, err) || !makeBuffers(st, err) || !setup(st, err)) return false;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < s.frames; ++i) if (!frame(st, i, i == s.frames - 1, err)) return false;
    out.msPerFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / s.frames;
    const uint64_t bytes = (uint64_t)st.w * st.h * 4;
    const void* m = dev.mapRead(st.stage, bytes, err);
    if (!m) return false;
    out.width = s.width; out.height = s.height;
    out.rgba.assign((const uint8_t*)m, (const uint8_t*)m + bytes);
    dev.unmap(st.stage);
    return true;
}
}
