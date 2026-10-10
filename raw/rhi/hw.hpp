#pragma once
// Hardware features beyond the RHI contract (HW workstream H1; bounds in
// evidence/hw-h1-0-bounds.json). The RHI stays the portable interface; this header
// asks the linked backend what the adapter offers and runs the functional checks
// that hold the answer to account. Every feature has a fallback, and the environment
// variable RAW_NATIVE_HW_DISABLE (a comma list of feature names, or "all") forces
// a feature off whatever the device says, so the fallbacks are tested on the same
// adapter. Backends without hardware features (null, WebGPU) report none.
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
namespace raw::rhi::hw {

// Feature names, as they appear in reports and in RAW_NATIVE_HW_DISABLE.
inline constexpr const char* kRayPipeline = "raytracing_pipeline";
inline constexpr const char* kRayQuery = "ray_query";
inline constexpr const char* kOpacityMicromap = "opacity_micromap";
inline constexpr const char* kReorder = "shader_execution_reordering";
inline constexpr const char* kMeshShader = "mesh_shader";
inline constexpr const char* kWaveOps = "wave_ops";
inline constexpr const char* kNative16 = "native_16bit";
inline constexpr const char* kInt64 = "int64_shader_ops";
inline constexpr const char* kCoopMatrix = "cooperative_matrix";
inline constexpr const char* kBindless = "bindless";
inline constexpr const char* kEnhancedBarriers = "enhanced_barriers";
inline constexpr const char* kWorkGraphs = "work_graphs";
inline constexpr const char* kAsyncCompute = "async_compute";
inline constexpr const char* kTimestamps = "timestamps";

// True when RAW_NATIVE_HW_DISABLE names the feature or says "all".
inline bool disabled(std::string_view feature){
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    if (!env) return false;
    std::string_view s(env);
    while (!s.empty()){
        const size_t c = s.find(',');
        std::string_view tok = s.substr(0, c);
        while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
        while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
        if (tok == "all" || tok == feature) return true;
        if (c == std::string_view::npos) break;
        s.remove_prefix(c + 1);
    }
    return false;
}

inline std::string jsonEscape(std::string_view s){
    std::string o;
    for (char c : s){
        if (c == '"' || c == '\\'){ o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20){ char b[8]; std::snprintf(b, sizeof b, "\\u%04x", (unsigned)c); o += b; }
        else o += c;
    }
    return o;
}

struct Feature {
    std::string name;
    std::string api;          // the query that answered, e.g. "D3D12_FEATURE_D3D12_OPTIONS5.RaytracingTier"
    std::string detail;       // tier, limits, or why it is unavailable
    bool device{false};       // what the device says
    bool forcedOff{false};    // RAW_NATIVE_HW_DISABLE named it
    bool supported() const { return device && !forcedOff; }
};

struct Probe {
    std::string backend, adapter, driver, error;
    std::vector<Feature> features;
    const Feature* find(std::string_view name) const {
        for (const Feature& f : features) if (f.name == name) return &f;
        return nullptr;
    }
    bool on(std::string_view name) const { const Feature* f = find(name); return f && f->supported(); }
    // Add a feature as the device reports it, applying RAW_NATIVE_HW_DISABLE.
    void add(const char* name, bool device, std::string api, std::string detail){
        features.push_back(Feature{name, std::move(api), std::move(detail), device, disabled(name)});
    }
    std::string json() const {
        std::string j = "{\"backend\":\"" + jsonEscape(backend) + "\",\"adapter\":\"" + jsonEscape(adapter) +
                        "\",\"driver\":\"" + jsonEscape(driver) + "\",\"error\":\"" + jsonEscape(error) + "\",\"features\":[";
        for (size_t i = 0; i < features.size(); ++i){
            const Feature& f = features[i];
            j += (i ? "," : "") + std::string("{\"name\":\"") + f.name + "\",\"supported\":" + (f.supported() ? "true" : "false") +
                 ",\"device\":" + (f.device ? "true" : "false") + ",\"forced_off\":" + (f.forcedOff ? "true" : "false") +
                 ",\"api\":\"" + jsonEscape(f.api) + "\",\"detail\":\"" + jsonEscape(f.detail) + "\"}";
        }
        return j + "]}";
    }
};

// Ask the linked backend about the device the RHI picked.
Probe probe(Device& dev);

// One functional check: does the device do what the probe says? (bounds: probe_function_agreement)
struct FunctionalCheck {
    std::string feature;
    bool probeSays{false};         // the probe reports it supported (after forced-off)
    bool pipelineCreated{false};   // the feature's pipeline was created
    bool ranFallback{false};       // the fallback ran in its place
    bool dispatched{false};
    bool match{false};             // the dispatched result is bit-equal to the CPU (true when nothing was dispatched)
    std::string detail;
    bool pass() const { return pipelineCreated == probeSays && (!dispatched || match); }
};
std::vector<FunctionalCheck> functionalChecks(Device& dev, const Probe& p, std::string& err);

// GPU time of a busy kernel at n and 2n iterations a thread (bounds: timestamp_linearity).
struct Timing {
    bool available{false};
    std::string error;
    double ticksPerSecond{0};
    uint32_t n{0};
    double msN{0}, ms2N{0};        // medians over the repetitions
    double ratio() const { return msN > 0 ? ms2N / msN : 0; }
};
// flat = true runs the control kernel, whose work ignores n.
Timing timestampLinearity(Device& dev, bool flat, int repetitions = 21);

}  // namespace raw::rhi::hw
