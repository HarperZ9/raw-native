// raw_native_cli hw-wave16 (raw/tools/hw_wave16_cmd.hpp; bounds in evidence/hw-h1-2-bounds.json).
#include "raw/tools/hw_wave16_cmd.hpp"
#include "hw_stats.hpp"
#include "raw/rhi/hw.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
namespace raw {
namespace {
namespace hw = rhi::hw;
using hwstats::num;
uint32_t hash32(uint32_t x){ x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
// The procedural HDR radiance image (values 0.01 to 1000): gradients, a texture, an edge, emitters, grain.
std::vector<float> hdrImage(uint32_t w, uint32_t h){
    std::vector<float> img(3ull * w * h);
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x){
        const float u = (float)x / w, v = (float)y / h;
        float base = 0.05f + 0.9f * u * v + 0.2f * std::sin(40.0f * u) * std::sin(30.0f * v) + (x > w / 2 ? 0.3f : 0.0f);
        for (int k = 0; k < 6; ++k){
            const float cx = 0.15f + 0.14f * k, cy = 0.3f + 0.08f * (k % 3), d2 = (u - cx) * (u - cx) + (v - cy) * (v - cy);
            base += 1000.0f * std::exp(-d2 * 4000.0f);
        }
        const float grain = 0.85f + 0.3f * (float)(hash32(y * w + x) & 0xFFFF) / 65535.0f;
        const float c[3] = {base * grain, base * grain * 0.8f + 0.02f, base * grain * 0.6f + 0.04f};
        for (int i = 0; i < 3; ++i) img[3ull * (y * w + x) + i] = std::fmin(std::fmax(c[i], 0.01f), 1000.0f);
    }
    return img;
}
// Elements outside |a - ref * scale| <= 2^-8 |ref * scale| + 1e-6 scale, and the worst ratio to that tolerance.
uint64_t over(const std::vector<float>& a, const std::vector<float>& ref, double scale, double& worst){
    if (a.size() != ref.size()) return ~0ull;
    uint64_t n = 0;
    worst = 0;
    for (size_t i = 0; i < ref.size(); ++i){
        const double r = ref[i] * scale, tol = std::ldexp(std::fabs(r), -8) + 1e-6 * scale, e = std::fabs((double)a[i] - r) / tol;
        if (e > worst) worst = e;
        if (e > 1) ++n;
    }
    return n;
}
std::string gain(const hw::KernelRun& fallback, const hw::KernelRun& fast){
    if (fallback.ms.empty() || fast.ms.empty()) return "null";
    double r = 0, lo = 0, hi = 0;
    hwstats::ratioInterval(fallback.ms, fast.ms, r, lo, hi);
    return "{\"median_ratio\":" + num(r) + ",\"ci95\":[" + num(lo) + "," + num(hi) + "]}";
}
uint64_t diffU(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b){
    if (a.size() != b.size()) return ~0ull;
    uint64_t d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += a[i] != b[i];
    return d;
}
std::string count(bool ran, uint64_t v){ return ran ? std::to_string(v) : "null"; }

// The scan half: CPU reference, both forms, the drop control.
std::string scanPart(rhi::Device& dev, int warmup, int repeats, bool& pass){
    std::vector<uint32_t> in(1u << 24), ref(in.size());
    uint32_t s = 0;
    for (uint32_t i = 0; i < in.size(); ++i){ in[i] = hash32(i); s += in[i]; ref[i] = s; }
    const hw::KernelRun sh = hw::scanFull(dev, in, hw::ScanForm::Shared, warmup, repeats);
    const hw::KernelRun wv = hw::scanFull(dev, in, hw::ScanForm::Wave, warmup, repeats);
    const hw::KernelRun dr = hw::scanFull(dev, in, hw::ScanForm::WaveDropControl, 0, 0);
    const bool forced = hw::disabled(hw::kWaveOps);
    const uint64_t dsh = sh.ran ? diffU(sh.u, ref) : ~0ull, dwv = wv.ran ? diffU(wv.u, ref) : ~0ull, ddr = dr.ran ? diffU(dr.u, ref) : 0;
    pass = sh.ran && dsh == 0 && (forced ? !wv.ran : (dwv == 0 && dr.ran && ddr > 0));
    return "{\"elements\":" + std::to_string(in.size()) + ",\"forced_off\":" + (forced ? "true" : "false") +
           ",\"shared_mismatches\":" + count(sh.ran, dsh) + ",\"wave_mismatches\":" + count(wv.ran, dwv) +
           ",\"control_mismatches\":" + count(dr.ran, ddr) + ",\"error\":\"" + hw::jsonEscape(sh.error + wv.error) +
           "\",\"shared_time\":" + hwstats::summary(sh.ms) + ",\"wave_time\":" + hwstats::summary(wv.ms) +
           ",\"wave_speedup\":" + gain(sh, wv) + ",\"pass\":" + (pass ? "true" : "false") + "}";
}
// The filter half: fp32 reference, fp16 form, the subnormal-range control.
std::string filterPart(rhi::Device& dev, int warmup, int repeats, bool& pass){
    const uint32_t w = 1920, h = 1080;
    const std::vector<float> img = hdrImage(w, h);
    std::vector<float> tiny(img.size());
    const double k = std::ldexp(1.0, -24);
    for (size_t i = 0; i < img.size(); ++i) tiny[i] = (float)(img[i] * k);
    const hw::KernelRun f32 = hw::bilateral(dev, img, w, h, false, warmup, repeats);
    const hw::KernelRun f16 = hw::bilateral(dev, img, w, h, true, warmup, repeats);
    const hw::KernelRun ctl = hw::bilateral(dev, tiny, w, h, true, 0, 0);
    const bool forced = hw::disabled(hw::kNative16);
    double worst = 0, worstCtl = 0;
    const uint64_t o = f16.ran && f32.ran ? over(f16.f, f32.f, 1.0, worst) : ~0ull;
    const uint64_t oc = ctl.ran && f32.ran ? over(ctl.f, f32.f, k, worstCtl) : 0;
    pass = f32.ran && (forced ? !f16.ran : (o == 0 && ctl.ran && oc > 0));
    return "{\"pixels\":" + std::to_string(w * h) + ",\"forced_off\":" + (forced ? "true" : "false") +
           ",\"fp16_over_bound\":" + count(f16.ran, o) + ",\"fp16_worst_over_tol\":" + num(worst) +
           ",\"control_over_bound\":" + count(ctl.ran, oc) + ",\"control_worst_over_tol\":" + num(worstCtl) +
           ",\"error\":\"" + hw::jsonEscape(f32.error + f16.error) + "\",\"fp32_time\":" + hwstats::summary(f32.ms) +
           ",\"fp16_time\":" + hwstats::summary(f16.ms) + ",\"fp16_speedup\":" + gain(f32, f16) + ",\"pass\":" + (pass ? "true" : "false") + "}";
}
}  // namespace

HwWave16Report hwWave16(rhi::Device& dev, int warmup, int repeats){
    HwWave16Report r;
    bool ps = false, pf = false;
    const std::string sj = scanPart(dev, warmup, repeats, ps);
    const std::string fj = filterPart(dev, warmup, repeats, pf);
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    r.pass = ps && pf;
    r.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-2-bounds.json\",\n \"adapter\": \"" +
             hw::jsonEscape(dev.adapter().description) + "\",\n \"disable\": \"" + hw::jsonEscape(env ? env : "") +
             "\",\n \"warmup\": " + std::to_string(warmup) + ",\n \"repeats\": " + std::to_string(repeats) +
             ",\n \"scan\": " + sj + ",\n \"bilateral\": " + fj + ",\n \"pass\": " + (r.pass ? "true" : "false") + "\n}\n";
    return r;
}

int hwWave16Command(int argc, char** argv){
    int warmup = 1, repeats = 5;
    std::string out;
    for (int i = 1; i < argc; ++i){
        if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) repeats = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli hw-wave16 [--warmup N] [--repeats N] [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev){ std::printf("{\"error\": \"%s\"}\n", rhi::hw::jsonEscape(why).c_str()); return 4; }
    const HwWave16Report r = hwWave16(*dev, warmup, repeats);
    std::fputs(r.json.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
}  // namespace raw
