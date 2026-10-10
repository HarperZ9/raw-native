// raw_native_cli hw-async (raw/tools/hw_async_cmd.hpp; bounds in evidence/hw-h1-4-bounds.json).
#include "raw/tools/hw_async_cmd.hpp"
#include "hw_stats.hpp"
#include "raw/rhi/hw.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
namespace raw {
namespace {
namespace hw = rhi::hw;
using hwstats::num;
// The CPU answer for Z (src/rhi/d3d12/hw_shaders/hw_async.hlsl).
std::vector<uint32_t> cpuZ(uint32_t n){
    std::vector<uint32_t> z(65536);
    auto run = [n](uint32_t h){ for (uint32_t i = 0; i < n; ++i){ h = h * 1664525u + 1013904223u; h ^= h >> 13u; } return h; };
    for (uint32_t i = 0; i < z.size(); ++i){
        const uint32_t x = run(i ^ 0x9E3779B9u), y = run(i ^ 0x85EBCA6Bu);
        z[i] = x ^ ((y << 7u) | (y >> 25u));
    }
    return z;
}
uint64_t diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b){
    if (a.size() != b.size()) return ~0ull;
    uint64_t d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += a[i] != b[i];
    return d;
}
}  // namespace

HwAsyncReport hwAsync(rhi::Device& dev, uint32_t n, int warmup, int repeats){
    HwAsyncReport rep;
    const std::vector<uint32_t> ref = cpuZ(n);
    std::vector<double> serial, async;
    uint64_t badSerial = 0, badAsync = 0;
    std::string err;
    bool asyncRan = true;
    for (int i = 0; i < warmup + repeats && err.empty(); ++i){
        const hw::AsyncRun s = hw::asyncSchedule(dev, hw::Schedule::Serial, n);
        if (!s.ran){ err = s.error; break; }
        badSerial += diff(s.z, ref);
        if (i >= warmup) serial.push_back(s.wallMs);
        const hw::AsyncRun a = hw::asyncSchedule(dev, hw::Schedule::Async, n);
        if (!a.ran){ asyncRan = false; if (!hw::disabled(hw::kAsyncCompute)) err = a.error; continue; }
        badAsync += diff(a.z, s.z) + diff(a.z, ref);
        if (i >= warmup) async.push_back(a.wallMs);
    }
    const hw::AsyncRun c = asyncRan ? hw::asyncSchedule(dev, hw::Schedule::WrongWaitControl, n) : hw::AsyncRun{};
    const uint64_t ctlDiff = c.ran ? diff(c.z, ref) : 0;
    const bool forced = hw::disabled(hw::kAsyncCompute);
    std::string gain = "null";
    if (!serial.empty() && !async.empty()){
        double r = 0, lo = 0, hi = 0;
        hwstats::ratioInterval(serial, async, r, lo, hi);
        gain = "{\"median_ratio\":" + num(r) + ",\"ci95\":[" + num(lo) + "," + num(hi) + "]}";
    }
    const bool equal = err.empty() && badSerial == 0 && (forced ? !asyncRan : (asyncRan && badAsync == 0));
    const bool ctlFails = forced || (c.ran && ctlDiff > 0);
    rep.pass = equal && ctlFails;
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    rep.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-4-bounds.json\",\n \"adapter\": \"" +
        hw::jsonEscape(dev.adapter().description) + "\",\n \"disable\": \"" + hw::jsonEscape(env ? env : "") + "\",\n \"n\": " +
        std::to_string(n) + ",\n \"schedule\": \"" + (forced ? "serial only (async_compute forced off)" : "serial and async") +
        "\",\n \"error\": \"" + hw::jsonEscape(err) + "\",\n \"serial_mismatches_vs_cpu\": " + std::to_string(badSerial) +
        ",\n \"async_mismatches\": " + std::to_string(badAsync) + ",\n \"control_ran\": " + (c.ran ? "true" : "false") +
        ",\n \"control_elements_differing\": " + std::to_string(ctlDiff) + ",\n \"serial_wall\": " + hwstats::summary(serial) +
        ",\n \"async_wall\": " + hwstats::summary(async) + ",\n \"overlap_gain\": " + gain + ",\n \"verdict\": {\"equal\": " +
        (equal ? "true" : "false") + ", \"control_fails\": " + (ctlFails ? "true" : "false") + ", \"pass\": " + (rep.pass ? "true" : "false") + "}\n}\n";
    return rep;
}

int hwAsyncCommand(int argc, char** argv){
    uint32_t n = 4096;
    int warmup = 2, repeats = 5;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--n") == 0 && i + 1 < argc) n = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli hw-async [--n N] [--warmup N] [--repeats N] [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev){ std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    const HwAsyncReport r = hwAsync(*dev, n, warmup, repeats);
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
}  // namespace raw
