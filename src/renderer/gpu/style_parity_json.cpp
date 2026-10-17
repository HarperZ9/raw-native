// The JSON record of a style-parity run (raw/renderer/style_parity.hpp).
#include "raw/renderer/style_parity.hpp"
#include "json_text.hpp"
#include <cstdio>
#include <string>
namespace raw::gpu_check {
namespace {
std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.6g", v); return b; }
const char* tf(bool b) { return b ? "true" : "false"; }
}  // namespace

std::string styleParityJson(const StyleParity& r) {
    std::string s = "{\n \"check\": \"style passes in the C++ core (ADR 0019)\",\n \"bounds\": \"evidence/cpp-port-style-bounds.json, evidence/cpp-port-style-bounds-2.json, -3.json, -4.json, -5.json, -6.json, -7.json, -8.json, -9.json\",\n";
    if (!r.only.empty()) s += " \"only\": \"" + jsonText(r.only) + "\", \"note\": \"a development run of one group, not the check of record\",\n";
    s += " \"backend\": \"" + jsonText(r.backend) + "\",\n \"adapter\": \"" + jsonText(r.adapter) + "\",\n";
    s += " \"error\": \"" + jsonText(r.error) + "\",\n \"gpu\": \"" + std::string(r.gpuWanted ? "run" : "skipped: no GPU backend or adapter") + "\",\n";
    s += " \"cases\": [\n";
    for (std::size_t k = 0; k < r.cases.size(); ++k) {
        const StyleCase& c = r.cases[k];
        s += "  {\"pass\": \"" + c.pass + "\", \"id\": \"" + jsonText(c.id) + "\", \"decisions\": " + std::to_string(c.decisions);
        s += ",\n   \"cpu\": {\"max_abs\": " + num(c.cpuMaxAbs) + ", \"mismatch\": " + std::to_string(c.cpuMismatch) + ", \"excused_ties\": " +
             std::to_string(c.cpuExcused) + ", \"secondary_mismatch\": " + std::to_string(c.cpuRgbaMismatch);
        if (c.pass == "ditherlight") s += std::string(", \"plan_equal\": ") + tf(c.planEqual);
        else if (c.pass != "paintover") {
            s += std::string(", \"preset_equal\": ") + tf(c.presetEqual);
            if (!c.presetDetail.empty()) s += ", \"preset_detail\": \"" + jsonText(c.presetDetail) + "\"";
            if (c.pass == "hatch") s += ", \"hit_mismatch\": " + std::to_string(c.hitMismatch) + ", \"fallback_mismatch\": " + std::to_string(c.fallbackMismatch);
        } else s += ", \"covered\": " + std::to_string(c.covered) + ", \"surface\": " + std::to_string(c.surface) +
                  ", \"view_shift_control_fails\": " + tf(c.viewShiftControlFails);
        s += std::string(", \"control_fails\": ") + tf(c.cpuControlFails) + ", \"pass\": " + tf(c.cpuPass) + "}";
        if (c.gpuRun)
            s += ",\n   \"gpu\": {\"mismatch\": " + std::to_string(c.gpuMismatch) + ", \"mismatch_fraction\": " + num(c.gpuMismatchFraction) +
                 ", \"over2_fraction\": " + num(c.gpuOver2Fraction) + ", \"mean\": " + num(c.gpuMean) + ", \"max\": " + std::to_string(c.gpuMax) + ", \"p999\": " + std::to_string(c.gpuP999) +
                 ", \"control_fails\": " + tf(c.gpuControlFails) + ", \"pass\": " + tf(c.gpuPass) + "}";
        s += k + 1 < r.cases.size() ? "},\n" : "}\n";
    }
    const LookKeysResult& L = r.lookkeys;
    s += " ],\n \"lookkeys\": {\"series\": " + std::to_string(L.series) + ", \"samples\": " + std::to_string(L.samples) + ", \"max_abs\": " + num(L.maxAbs) +
         ", \"keys_exact\": " + tf(L.keysExact) + ", \"refusals_match\": " + tf(L.refusalsMatch) + ", \"unknown_refused\": " + tf(L.unknownRefused) +
         ", \"control_fails\": " + tf(L.controlFails) + ", \"pass\": " + tf(L.pass()) + "},\n";
    const PostStackResult& P = r.poststack;
    s += " \"poststack\": {\"lists\": " + std::to_string(P.lists) + ", \"plan_mismatch\": " + std::to_string(P.planMismatch) +
         ", \"plan_control_differs\": " + tf(P.planControlDiffers) + ", \"chains\": [\n";
    for (std::size_t k = 0; k < P.chains.size(); ++k) {
        const PostChain& c = P.chains[k];
        s += "   {\"id\": \"" + jsonText(c.id) + "\", \"values\": " + std::to_string(c.values) + ", \"cpu\": {\"max_ulp\": " + num(c.cpuMaxUlp) +
             ", \"differing_fraction\": " + num(c.cpuDifferingFraction) + ", \"control_fails\": " + tf(c.cpuControlFails) + ", \"pass\": " + tf(c.cpuPass) + "}";
        if (c.gpuRun) s += ", \"gpu\": {\"max_rel\": " + num(c.gpuMaxRel) + ", \"control_fails\": " + tf(c.gpuControlFails) + ", \"pass\": " + tf(c.gpuPass) + "}";
        s += k + 1 < P.chains.size() ? "},\n" : "}\n";
    }
    s += std::string("  ], \"cpu_pass\": ") + tf(P.cpuPass()) + ", \"gpu_pass\": " + tf(r.gpuWanted && P.gpuPass()) + "},\n";
    s += std::string(" \"cpu_pass\": ") + tf(r.cpuPass()) + ",\n \"gpu_pass\": " + tf(r.gpuWanted && r.gpuPass()) + ",\n \"pass\": " + tf(r.pass()) + "\n}\n";
    return s;
}

}  // namespace raw::gpu_check
