// HW H1.0, Vulkan half: functional checks, timestamp linearity and the run record
// (raw/rhi/hw_vk.hpp; bounds in evidence/hw-h1-0-vk-bounds.json).
#include "vk_context.hpp"
#include "raw/rhi/hw_vk.hpp"
#include "raw_vk_spirv.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace raw::rhi::hw {
namespace {
using vk::Buffer;
using vk::Context;
using vk::Pipeline;

const VkSpirvBlob* spirv(const char* name){
    for (const VkSpirvBlob& b : kVkSpirv) if (std::strcmp(b.name, name) == 0) return &b;
    return nullptr;
}
bool make(Context& c, const char* name, uint32_t bufs, uint32_t push, Pipeline& p, std::string& err, bool accel = false){
    const VkSpirvBlob* b = spirv(name);
    if (!b){ err = std::string("no SPIR-V named ") + name; return false; }
    return c.pipeline(b->words, b->bytes, bufs, push, p, err, accel);
}
uint32_t hash32(uint32_t x){ x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
// Attempt the feature's pipeline only when the device offers it and it is not forced off.
bool attempt(const Probe& p, const char* f){ const Feature* x = p.find(f); return x && x->device && !x->forcedOff; }

FunctionalCheck scan(Context& c){
    FunctionalCheck k; k.feature = kWaveOps; k.probeSays = c.probe().on(kWaveOps);
    constexpr uint32_t kGroups = 1024, kN = kGroups * 64;
    Buffer src, dst, lanes; Pipeline p; std::string err;
    if (!c.buffer(4ull * kN, src, err) || !c.buffer(4ull * kN, dst, err) || !c.buffer(4ull * kN, lanes, err)){ k.detail = err; return k; }
    auto* in = (uint32_t*)src.map;
    for (uint32_t i = 0; i < kN; ++i) in[i] = hash32(i) & 0xFFFFu;
    if (attempt(c.probe(), kWaveOps)) k.pipelineCreated = make(c, "vk_scan_subgroup", 3, 0, p, err);
    if (!k.pipelineCreated){
        k.detail = attempt(c.probe(), kWaveOps) ? "subgroup pipeline refused: " + err + "; " : "not attempted; ";
        if (!make(c, "vk_scan_shared", 3, 0, p, err)){ k.detail += "fallback refused: " + err; return k; }
        k.ranFallback = true;
    }
    if (c.dispatch(p, {&src, &dst, &lanes}, nullptr, kGroups, nullptr, err)){
        k.dispatched = true;
        const auto* got = (const uint32_t*)dst.map;
        uint64_t bad = 0;
        for (uint32_t g = 0; g < kGroups; ++g){ uint32_t s = 0; for (uint32_t i = 0; i < 64; ++i){ s += in[g * 64 + i]; if (got[g * 64 + i] != s) ++bad; } }
        k.match = bad == 0;
        k.detail += (k.ranFallback ? std::string("shared-memory scan") : "subgroup scan, " + std::to_string(((uint32_t*)lanes.map)[0]) + " lanes") +
                    ", " + std::to_string(bad) + " of " + std::to_string(kN) + " differ from the CPU";
    } else k.detail += "dispatch failed: " + err;
    c.destroy(p); c.destroy(src); c.destroy(dst); c.destroy(lanes);
    return k;
}

FunctionalCheck half(Context& c){
    FunctionalCheck k; k.feature = kNative16; k.probeSays = c.probe().on(kNative16);
    constexpr uint32_t kGroups = 64, kN = kGroups * 64;
    Buffer src, dst; Pipeline p; std::string err;
    if (!c.buffer(12ull * kN, src, err) || !c.buffer(4ull * (kN + 1), dst, err)){ k.detail = err; return k; }
    auto* in = (float*)src.map;
    for (uint32_t i = 0; i < kN; ++i){
        in[3 * i] = (float)((int)(hash32(3 * i) % 17) - 8) * 0.25f;
        in[3 * i + 1] = (float)((int)(hash32(3 * i + 1) % 17) - 8) * 0.25f;
        in[3 * i + 2] = (float)((int)(hash32(3 * i + 2) % 129) - 64) * 0.0625f;
    }
    if (attempt(c.probe(), kNative16)) k.pipelineCreated = make(c, "vk_half", 2, 0, p, err);
    if (!k.pipelineCreated){
        k.detail = attempt(c.probe(), kNative16) ? "float16 pipeline refused: " + err + "; " : "not attempted; ";
        if (!make(c, "vk_half_fallback", 2, 0, p, err)){ k.detail += "fallback refused: " + err; return k; }
        k.ranFallback = true;
    }
    if (c.dispatch(p, {&src, &dst}, nullptr, kGroups, nullptr, err)){
        k.dispatched = true;
        const auto* got = (const float*)dst.map;
        uint64_t bad = 0;
        for (uint32_t i = 0; i < kN; ++i){ const float w = in[3 * i] * in[3 * i + 1] + in[3 * i + 2]; if (std::memcmp(&w, &got[i + 1], 4)) ++bad; }
        const bool sixteen = got[0] == 1.0f;
        k.match = bad == 0 && sixteen == !k.ranFallback;
        k.detail += std::string(sixteen ? "ran at 16 bits" : "ran at 32 bits") + ", " + std::to_string(bad) + " of " + std::to_string(kN) + " differ";
    } else k.detail += "dispatch failed: " + err;
    c.destroy(p); c.destroy(src); c.destroy(dst);
    return k;
}

FunctionalCheck pipelineOnly(Context& c, const char* feature, const char* shader, uint32_t bufs, bool accel){
    FunctionalCheck k; k.feature = feature; k.probeSays = c.probe().on(feature);
    Pipeline p; std::string err;
    if (attempt(c.probe(), feature)){
        k.pipelineCreated = make(c, shader, bufs, 0, p, err, accel);
        k.detail = k.pipelineCreated ? "pipeline created (results are a later slice)" : "pipeline refused: " + err;
        c.destroy(p);
    } else k.detail = "not attempted: not offered or forced off";
    return k;
}

double median(std::vector<double> v){ std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }
Timing timing(Context& c, bool flat){
    Timing t;
    if (!c.probe().on(kTimestamps) || !c.timestampsUsable()){ t.error = "timestamps not offered or forced off"; return t; }
    t.ticksPerSecond = 1e9 / c.timestampPeriodNs();
    constexpr uint32_t kGroups = 1024;
    Buffer out; Pipeline p;
    if (!c.buffer(4ull * kGroups * 64, out, t.error) || !make(c, flat ? "vk_busy_flat" : "vk_busy", 1, 4, p, t.error)) return t;
    auto run = [&](uint32_t n, double& ms){ return c.dispatch(p, {&out}, &n, kGroups, &ms, t.error); };
    double ms = 0;
    uint32_t n = 1024;
    if (!run(n, ms)) return t;
    while (!flat && n < (1u << 24)){ if (!run(n, ms)) return t; if (ms >= 1.0) break; n *= 2; }
    if (flat) n = 1u << 16;
    std::vector<double> a, b;
    for (int r = 0; r < 21; ++r){ double x = 0, y = 0; if (!run(n, x) || !run(2 * n, y)) return t; a.push_back(x); b.push_back(y); }
    t.n = n; t.msN = median(a); t.ms2N = median(b); t.available = true;
    c.destroy(p); c.destroy(out);
    return t;
}
std::string num(double v){ char b[48]; std::snprintf(b, sizeof b, "%.6g", v); return b; }
std::string tjson(const Timing& t){
    if (!t.available) return "{\"available\":false,\"error\":\"" + jsonEscape(t.error) + "\"}";
    return "{\"available\":true,\"ticks_per_second\":" + num(t.ticksPerSecond) + ",\"n\":" + std::to_string(t.n) + ",\"ms_n\":" + num(t.msN) +
           ",\"ms_2n\":" + num(t.ms2N) + ",\"ratio\":" + num(t.ratio()) + "}";
}
bool linear(const Timing& t){ return t.available && t.ratio() >= 1.8 && t.ratio() <= 2.2; }
}  // namespace

VkRun vulkanProbe(bool checks){
    VkRun r;
    Context c;
    std::string err;
    if (!c.init(err)){ r.json = "{\"error\": \"" + jsonEscape(err) + "\"}\n"; r.code = 4; return r; }
    if (!checks){ r.json = c.probe().json() + "\n"; return r; }
    std::vector<FunctionalCheck> fc{scan(c), half(c), pipelineOnly(c, kRayQuery, "vk_rayquery_probe", 1, true),
                                    pipelineOnly(c, kCoopMatrix, "vk_coopmat_probe", 3, false)};
    const Timing t = timing(c, false), ctl = timing(c, true);
    bool agree = true;
    std::string cj = "[";
    for (size_t i = 0; i < fc.size(); ++i){
        const FunctionalCheck& k = fc[i];
        // Vulkan never attempts an unoffered feature, so agreement is: attempted => created, and dispatched => match.
        const bool pass = (k.pipelineCreated == k.probeSays) && (!k.dispatched || k.match);
        agree = agree && pass;
        cj += (i ? "," : "") + std::string("{\"feature\":\"") + k.feature + "\",\"probe_says\":" + (k.probeSays ? "true" : "false") +
              ",\"pipeline_created\":" + (k.pipelineCreated ? "true" : "false") + ",\"ran_fallback\":" + (k.ranFallback ? "true" : "false") +
              ",\"dispatched\":" + (k.dispatched ? "true" : "false") + ",\"match\":" + (!k.dispatched ? "null" : k.match ? "true" : "false") +
              ",\"pass\":" + (pass ? "true" : "false") + ",\"detail\":\"" + jsonEscape(k.detail) + "\"}";
    }
    cj += "]";
    const bool controlFails = ctl.available && !linear(ctl);
    const bool clean = c.validationErrors() == 0;
    const bool pass = agree && linear(t) && controlFails && clean;
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    r.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-0-vk-bounds.json\",\n \"disable\": \"" + jsonEscape(env ? env : "") +
             "\",\n \"probe\": " + c.probe().json() + ",\n \"functional\": " + cj + ",\n \"timestamp_linearity\": " + tjson(t) +
             ",\n \"timestamp_control\": " + tjson(ctl) + ",\n \"validation\": {\"enabled\": " +
             (std::getenv("RAW_NATIVE_VK_VALIDATION") ? "true" : "false") + ", \"errors\": " + std::to_string(c.validationErrors()) +
             ", \"first\": \"" + jsonEscape(c.firstValidationError()) +
             "\", \"loader_errors\": " + std::to_string(c.loaderErrors()) + ", \"first_loader\": \"" + jsonEscape(c.firstLoaderError()) + "\"},\n \"verdict\": {\"probe_function_agreement\": " + (agree ? "true" : "false") +
             ", \"timestamp_linearity\": " + (linear(t) ? "true" : "false") + ", \"control_fails\": " + (controlFails ? "true" : "false") +
             ", \"validation_clean\": " + (clean ? "true" : "false") + ", \"pass\": " + (pass ? "true" : "false") + "}\n}\n";
    r.code = pass ? 0 : 1;
    return r;
}
}  // namespace raw::rhi::hw
