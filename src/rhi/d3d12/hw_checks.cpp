// HW H1.0: the functional checks behind the probe, and the timestamp linearity measurement
// (raw/rhi/hw.hpp; bounds in evidence/hw-h1-0-bounds.json).
#include "hw_queue.hpp"
#include "raw/rhi/hw.hpp"
#include "raw_hw_dxil.hpp"
#include <algorithm>
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwPipeline;
using d3d12::HwQueue;

const HwDxilBlob* blob(const char* name){
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, name) == 0) return &b;
    return nullptr;
}
bool makePipeline(HwQueue& q, const char* name, uint32_t uavs, uint32_t constants, bool srv, HwPipeline& out, std::string& err){
    const HwDxilBlob* b = blob(name);
    if (!b){ err = std::string("no DXIL named ") + name; return false; }
    return q.pipeline(b->bytes, b->size, uavs, constants, srv, out, err);
}
// Upload `in`, run the pipeline over `groups` groups with `outs` output buffers of the given
// sizes, and read every output back. Three submissions so every buffer starts and ends in
// COMMON and no explicit transition is needed (buffers decay to COMMON after each).
bool run(HwQueue& q, const HwPipeline& p, const void* in, uint64_t inSize, const std::vector<uint64_t>& outSizes,
         uint32_t groups, std::vector<std::vector<uint8_t>>& outs, std::string& err){
    ComPtr<ID3D12Resource> up, src;
    if (!q.uploadBuffer(in, inSize, up, err) || !q.uavBuffer(inSize, src, err)) return false;
    std::vector<ComPtr<ID3D12Resource>> dst(outSizes.size()), rb(outSizes.size());
    for (size_t i = 0; i < outSizes.size(); ++i)
        if (!q.uavBuffer(outSizes[i], dst[i], err) || !q.readbackBuffer(outSizes[i], rb[i], err)) return false;
    if (!q.begin(err)) return false;
    q.copy(src.Get(), up.Get(), inSize);
    if (!q.submitAndWait(0, err) || !q.begin(err)) return false;
    std::vector<ID3D12Resource*> uavs{src.Get()};
    for (auto& d : dst) uavs.push_back(d.Get());
    q.dispatch(p, uavs, nullptr, 0, groups, 1, 1);
    if (!q.submitAndWait(0, err) || !q.begin(err)) return false;
    for (size_t i = 0; i < dst.size(); ++i) q.copy(rb[i].Get(), dst[i].Get(), outSizes[i]);
    if (!q.submitAndWait(0, err)) return false;
    outs.assign(dst.size(), {});
    for (size_t i = 0; i < dst.size(); ++i){
        outs[i].resize(outSizes[i]);
        if (!q.readBack(rb[i].Get(), outs[i].data(), outSizes[i], err)) return false;
    }
    return true;
}
uint32_t hash32(uint32_t x){ x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

FunctionalCheck scanCheck(HwQueue& q, const Probe& pr){
    FunctionalCheck c; c.feature = kWaveOps; c.probeSays = pr.on(kWaveOps);
    const Feature* f = pr.find(kWaveOps);
    const bool attempt = f && !f->forcedOff;
    constexpr uint32_t kGroups = 1024, kN = kGroups * 64;
    std::vector<uint32_t> in(kN);
    for (uint32_t i = 0; i < kN; ++i) in[i] = hash32(i) & 0xFFFFu;
    HwPipeline p;
    std::string err;
    if (attempt) c.pipelineCreated = makePipeline(q, "hw_scan_wave", 3, 0, false, p, err);
    if (!c.pipelineCreated){
        c.detail = attempt ? "wave pipeline refused: " + err + "; " : "forced off; ";
        err.clear();
        if (!makePipeline(q, "hw_scan_shared", 3, 0, false, p, err)){ c.detail += "fallback refused: " + err; return c; }
        c.ranFallback = true;
    }
    std::vector<std::vector<uint8_t>> outs;
    if (!run(q, p, in.data(), 4ull * kN, {4ull * kN, 4ull * kN}, kGroups, outs, err)){ c.detail += "run failed: " + err; return c; }
    c.dispatched = true;
    const uint32_t* got = (const uint32_t*)outs[0].data();
    const uint32_t* lanes = (const uint32_t*)outs[1].data();
    uint64_t bad = 0;
    for (uint32_t g = 0; g < kGroups; ++g){
        uint32_t s = 0;
        for (uint32_t i = 0; i < 64; ++i){ s += in[g * 64 + i]; if (got[g * 64 + i] != s) ++bad; }
    }
    c.match = bad == 0;
    c.detail += (c.ranFallback ? "group-shared scan" : "wave scan, " + std::to_string(lanes[0]) + " lanes") +
                ", " + std::to_string(kN) + " values, " + std::to_string(bad) + " differ from the CPU";
    return c;
}

FunctionalCheck halfCheck(HwQueue& q, const Probe& pr){
    FunctionalCheck c; c.feature = kNative16; c.probeSays = pr.on(kNative16);
    const Feature* f = pr.find(kNative16);
    const bool attempt = f && !f->forcedOff;
    constexpr uint32_t kGroups = 64, kN = kGroups * 64;
    // a, b in {-2, -1.75, ..., 2}; c in multiples of 1/16 within [-4, 4]: a * b + c is exact in half and float.
    std::vector<float> in(3 * kN);
    for (uint32_t i = 0; i < kN; ++i){
        in[3 * i] = (float)((int)(hash32(3 * i) % 17) - 8) * 0.25f;
        in[3 * i + 1] = (float)((int)(hash32(3 * i + 1) % 17) - 8) * 0.25f;
        in[3 * i + 2] = (float)((int)(hash32(3 * i + 2) % 129) - 64) * 0.0625f;
    }
    HwPipeline p;
    std::string err;
    if (attempt) c.pipelineCreated = makePipeline(q, "hw_half", 2, 0, false, p, err);
    if (!c.pipelineCreated){
        c.detail = attempt ? "16-bit pipeline refused: " + err + "; " : "forced off; ";
        err.clear();
        if (!makePipeline(q, "hw_half_fallback", 2, 0, false, p, err)){ c.detail += "fallback refused: " + err; return c; }
        c.ranFallback = true;
    }
    std::vector<std::vector<uint8_t>> outs;
    if (!run(q, p, in.data(), 4ull * in.size(), {4ull * (kN + 1)}, kGroups, outs, err)){ c.detail += "run failed: " + err; return c; }
    c.dispatched = true;
    const float* got = (const float*)outs[0].data();
    uint64_t bad = 0;
    for (uint32_t i = 0; i < kN; ++i){
        const float want = in[3 * i] * in[3 * i + 1] + in[3 * i + 2];
        if (std::memcmp(&want, &got[i + 1], 4) != 0) ++bad;
    }
    const bool sixteen = got[0] == 1.0f;
    c.match = bad == 0 && sixteen == !c.ranFallback;
    c.detail += std::string(sixteen ? "arithmetic ran at 16 bits" : "arithmetic ran at 32 bits") + ", " +
                std::to_string(bad) + " of " + std::to_string(kN) + " results differ from the CPU";
    return c;
}

FunctionalCheck rayQueryCheck(HwQueue& q, const Probe& pr){
    FunctionalCheck c; c.feature = kRayQuery; c.probeSays = pr.on(kRayQuery);
    const Feature* f = pr.find(kRayQuery);
    HwPipeline p;
    std::string err;
    if (f && !f->forcedOff) c.pipelineCreated = makePipeline(q, "hw_rayquery_probe", 1, 0, true, p, err);
    c.detail = c.pipelineCreated ? "ray query pipeline created (dispatch is H1.1)"
             : (f && f->forcedOff) ? "forced off" : "ray query pipeline refused: " + err;
    return c;
}

double median(std::vector<double> v){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}  // namespace

std::vector<FunctionalCheck> functionalChecks(Device& dev, const Probe& p, std::string& err){
    HwQueue q;
    if (!q.init(static_cast<d3d12::D3d12Device&>(dev).d3d(), D3D12_COMMAND_LIST_TYPE_COMPUTE, 0, err)) return {};
    return {scanCheck(q, p), halfCheck(q, p), rayQueryCheck(q, p)};
}

Timing timestampLinearity(Device& dev, bool flat, int reps){
    Timing t;
    HwQueue q;
    if (disabled(kTimestamps)){ t.error = "timestamps forced off"; return t; }
    if (!q.init(static_cast<d3d12::D3d12Device&>(dev).d3d(), D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, t.error)) return t;
    t.ticksPerSecond = q.ticksPerSecond();
    HwPipeline p;
    if (!makePipeline(q, flat ? "hw_busy_flat" : "hw_busy", 1, 1, false, p, t.error)) return t;
    constexpr uint32_t kGroups = 1024;
    ComPtr<ID3D12Resource> out;
    if (!q.uavBuffer(4ull * kGroups * 64, out, t.error)) return t;
    auto measure = [&](uint32_t n, double& ms) -> bool {
        if (!q.begin(t.error)) return false;
        q.timestamp(0);
        q.dispatch(p, {out.Get()}, &n, 0, kGroups, 1, 1);
        q.timestamp(1);
        if (!q.submitAndWait(2, t.error)) return false;
        ms = 1000.0 * (double)q.ticks(0, 1) / t.ticksPerSecond;
        return true;
    };
    double ms = 0;
    if (!measure(1024, ms)) return t;   // warm-up: pipeline and residency
    uint32_t n = 1024;
    // The non-flat kernel picks n so its run takes at least 1 ms; the flat control uses the same
    // search on the real kernel's terms (n only matters to the real kernel).
    while (n < (1u << 24)){
        if (!measure(n, ms)) return t;
        if (flat || ms >= 1.0) break;
        n *= 2;
    }
    if (flat) n = 1u << 16;
    std::vector<double> a, b;
    for (int r = 0; r < reps; ++r){
        double x = 0, y = 0;
        if (!measure(n, x) || !measure(2 * n, y)) return t;
        a.push_back(x); b.push_back(y);
    }
    t.n = n; t.msN = median(a); t.ms2N = median(b);
    t.available = true;
    return t;
}
}  // namespace raw::rhi::hw
