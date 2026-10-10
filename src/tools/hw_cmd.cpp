// raw_native_cli hw-probe (raw/tools/hw_cmd.hpp).
#include "raw/tools/hw_cmd.hpp"
#include "raw/rhi/hw.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
namespace raw {
namespace {
std::string num(double v){ char b[48]; std::snprintf(b, sizeof b, "%.6g", v); return b; }
std::string timingJson(const rhi::hw::Timing& t){
    if (!t.available) return "{\"available\":false,\"error\":\"" + rhi::hw::jsonEscape(t.error) + "\"}";
    return "{\"available\":true,\"ticks_per_second\":" + num(t.ticksPerSecond) + ",\"n\":" + std::to_string(t.n) +
           ",\"ms_n\":" + num(t.msN) + ",\"ms_2n\":" + num(t.ms2N) + ",\"ratio\":" + num(t.ratio()) + "}";
}
bool linear(const rhi::hw::Timing& t){ return t.available && t.ratio() >= 1.8 && t.ratio() <= 2.2; }
}  // namespace

HwReport hwChecks(rhi::Device& dev){
    namespace hw = rhi::hw;
    const hw::Probe p = hw::probe(dev);
    std::string err;
    const std::vector<hw::FunctionalCheck> checks = hw::functionalChecks(dev, p, err);
    const hw::Timing t = hw::timestampLinearity(dev, false);
    const hw::Timing c = hw::timestampLinearity(dev, true);
    bool agree = !checks.empty();
    std::string cj = "[";
    for (size_t i = 0; i < checks.size(); ++i){
        const hw::FunctionalCheck& k = checks[i];
        agree = agree && k.pass();
        cj += (i ? "," : "") + std::string("{\"feature\":\"") + k.feature + "\",\"probe_says\":" + (k.probeSays ? "true" : "false") +
              ",\"pipeline_created\":" + (k.pipelineCreated ? "true" : "false") + ",\"ran_fallback\":" + (k.ranFallback ? "true" : "false") +
              ",\"dispatched\":" + (k.dispatched ? "true" : "false") + ",\"match\":" + (!k.dispatched ? "null" : k.match ? "true" : "false") +
              ",\"pass\":" + (k.pass() ? "true" : "false") + ",\"detail\":\"" + hw::jsonEscape(k.detail) + "\"}";
    }
    cj += "]";
    // Timing passes when linear; the control must not be (a control that passes is non-discriminating).
    const bool timingPass = linear(t);
    const bool controlFails = c.available && !linear(c);
    HwReport r;
    r.pass = agree && timingPass && controlFails;
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    r.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-0-bounds.json\",\n \"disable\": \"" +
             hw::jsonEscape(env ? env : "") + "\",\n \"probe\": " + p.json() + ",\n \"functional\": " + cj +
             (err.empty() ? "" : ",\n \"functional_error\": \"" + hw::jsonEscape(err) + "\"") +
             ",\n \"timestamp_linearity\": " + timingJson(t) + ",\n \"timestamp_control\": " + timingJson(c) +
             ",\n \"verdict\": {\"probe_function_agreement\": " + (agree ? "true" : "false") +
             ", \"timestamp_linearity\": " + (timingPass ? "true" : "false") +
             ", \"control_fails\": " + (controlFails ? "true" : "false") + ", \"pass\": " + (r.pass ? "true" : "false") + "}\n}\n";
    return r;
}

int hwProbeCommand(int argc, char** argv){
    bool checks = false;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--checks") == 0) checks = true;
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli hw-probe [--checks] [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev){ std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    HwReport r;
    if (checks) r = hwChecks(*dev);
    else { r.json = rhi::hw::probe(*dev).json() + "\n"; r.pass = true; }
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
}  // namespace raw
