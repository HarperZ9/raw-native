// HW H1.7: cooperative matrix GEMM against a float64 reference, with controls, the fallback
// and timings (raw/rhi/hw_vk.hpp; bounds in evidence/hw-h1-7-bounds.json).
#include "vk_context.hpp"
#include "raw/rhi/hw_vk.hpp"
#include "raw_vk_spirv.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace raw::rhi::hw {
namespace {
using vk::Buffer;
using vk::Context;
using vk::Pipeline;

uint64_t mix(uint64_t& s){ uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
// fp32 to fp16, round to nearest even (inputs here are in [-1, 1], so no overflow handling is needed beyond the general path).
uint16_t toHalf(float f){
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (e <= 0){
        if (e < -10) return (uint16_t)sign;
        m |= 0x800000u;
        const uint32_t shift = (uint32_t)(14 - e), r = m >> shift, rem = m & ((1u << shift) - 1), half = 1u << (shift - 1);
        return (uint16_t)(sign | (r + (rem > half || (rem == half && (r & 1u)))));
    }
    if (e >= 31) return (uint16_t)(sign | 0x7C00u);
    uint32_t h = sign | ((uint32_t)e << 10) | (m >> 13);
    const uint32_t rem = m & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
    return (uint16_t)h;
}
float fromHalf(uint16_t h){
    const uint32_t sign = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    if (e == 0) return (sign ? -1.0f : 1.0f) * std::ldexp((float)m, -24);
    uint32_t x = sign | ((e + 112u) << 23) | (m << 13);
    float f; std::memcpy(&f, &x, 4); return f;
}
const VkSpirvBlob* spirv(const char* name){
    for (const VkSpirvBlob& b : kVkSpirv) if (std::strcmp(b.name, name) == 0) return &b;
    return nullptr;
}
double pct(std::vector<double> v, double p){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double k = p * (double)(v.size() - 1); const size_t i = (size_t)k;
    return i + 1 < v.size() ? v[i] + (k - (double)i) * (v[i + 1] - v[i]) : v[i];
}
std::string num(double v){ char b[48]; std::snprintf(b, sizeof b, "%.6g", v); return b; }

struct Check { uint64_t checked{0}, over{0}; double worst{0}, rmsRatio{0}; bool pass() const { return checked > 0 && over == 0; } };
// The committed accuracy check: the top-left 128 x 128 block and 8,192 seeded elements.
Check verify(const std::vector<uint16_t>& A, const std::vector<uint16_t>& B, const std::vector<float>& C, uint32_t n){
    Check c;
    std::vector<std::pair<uint32_t, uint32_t>> at;
    for (uint32_t i = 0; i < std::min(n, 128u); ++i) for (uint32_t j = 0; j < std::min(n, 128u); ++j) at.push_back({i, j});
    uint64_t s = 0xC0FFEEull + n;
    for (int k = 0; k < 8192; ++k) at.push_back({(uint32_t)(mix(s) % n), (uint32_t)(mix(s) % n)});
    double sse = 0, sseCpu = 0;
    for (auto [i, j] : at){
        double ref = 0, absSum = 0; float seq = 0;
        for (uint32_t k = 0; k < n; ++k){
            const double p = (double)fromHalf(A[(size_t)i * n + k]) * fromHalf(B[(size_t)k * n + j]);
            ref += p; absSum += std::fabs(p); seq += (float)p;
        }
        const double err = std::fabs((double)C[(size_t)i * n + j] - ref), tol = n * std::ldexp(1.0, -23) * absSum + 1e-6;
        c.worst = std::max(c.worst, err / tol);
        if (err > tol) ++c.over;
        sse += err * err; sseCpu += ((double)seq - ref) * ((double)seq - ref);
        ++c.checked;
    }
    c.rmsRatio = sseCpu > 0 ? std::sqrt(sse / sseCpu) : 0;
    return c;
}
// The ratio of medians a / b with a 95% bootstrap interval (10,000 resamples).
void bootstrap(const std::vector<double>& a, const std::vector<double>& b, double& lo, double& hi){
    uint64_t s = 0xB007ull;
    std::vector<double> ratios, ra(a.size()), rb(b.size());
    for (int it = 0; it < 10000; ++it){
        for (auto& x : ra) x = a[mix(s) % a.size()];
        for (auto& x : rb) x = b[mix(s) % b.size()];
        ratios.push_back(pct(ra, 0.5) / pct(rb, 0.5));
    }
    lo = pct(ratios, 0.025); hi = pct(ratios, 0.975);
}

struct Run { bool ran{false}; std::string error; Check check; std::vector<double> ms; };
Run runKernel(Context& c, const char* shader, uint32_t local, Buffer& a, Buffer& b, const std::vector<uint16_t>& A,
              const std::vector<uint16_t>& B, uint32_t n, int warmup, int repeats){
    Run r;
    const VkSpirvBlob* blob = spirv(shader);
    Pipeline p; Buffer cbuf;
    if (!blob){ r.error = std::string("no SPIR-V named ") + shader; return r; }
    if (!c.pipeline(blob->words, blob->bytes, 3, 12, p, r.error) || !c.deviceBuffer(4ull * n * n, cbuf, r.error)) return r;
    const uint32_t push[3] = {n, n, n};
    for (int i = 0; i < 1 + warmup + repeats; ++i){
        double ms = 0;
        if (!c.dispatch(p, {&a, &b, &cbuf}, push, n / local, &ms, r.error, n / local)){ c.destroy(p); c.destroy(cbuf); return r; }
        if (i > warmup) r.ms.push_back(ms);
    }
    std::vector<float> C((size_t)n * n);
    if (c.download(cbuf, C.data(), 4ull * n * n, r.error)){ r.check = verify(A, B, C, n); r.ran = true; }
    c.destroy(p); c.destroy(cbuf);
    return r;
}
std::string runJson(const Run& r, uint32_t n){
    if (!r.ran) return "{\"ran\":false,\"error\":\"" + jsonEscape(r.error) + "\"}";
    std::string j = "{\"ran\":true,\"checked\":" + std::to_string(r.check.checked) + ",\"over_bound\":" + std::to_string(r.check.over) +
                    ",\"worst_over_bound\":" + num(r.check.worst) + ",\"rms_vs_cpu_fp32\":" + num(r.check.rmsRatio) +
                    ",\"within_bound\":" + (r.check.pass() ? "true" : "false");
    if (!r.ms.empty()){
        const double med = pct(r.ms, 0.5);
        j += ",\"timing\":{\"n\":" + std::to_string(r.ms.size()) + ",\"median_ms\":" + num(med) + ",\"p5_ms\":" + num(pct(r.ms, 0.05)) +
             ",\"p95_ms\":" + num(pct(r.ms, 0.95)) + ",\"p99_ms\":" + num(pct(r.ms, 0.99)) + ",\"iqr_ms\":" + num(pct(r.ms, 0.75) - pct(r.ms, 0.25)) +
             ",\"tflops_at_median\":" + num(2.0 * n * n * (double)n / (med * 1e-3) / 1e12) + "}";
    }
    return j + "}";
}
}  // namespace

VkRun vulkanGemm(const std::vector<uint32_t>& sizes, int warmup, int repeats){
    VkRun out;
    Context c;
    std::string err;
    if (!c.init(err)){ out.json = "{\"error\": \"" + jsonEscape(err) + "\"}\n"; out.code = 4; return out; }
    const Feature* f = c.probe().find(kCoopMatrix);
    const bool coop = f && f->device && !f->forcedOff;
    bool pass = true;
    std::string sj;
    for (size_t si = 0; si < sizes.size(); ++si){
        const uint32_t n = sizes[si];
        std::vector<uint16_t> A((size_t)n * n), B((size_t)n * n);
        uint64_t s = 0xA11CEull + n;
        for (auto& x : A) x = toHalf((float)((double)(mix(s) >> 11) * 0x1.0p-53 * 2 - 1));
        for (auto& x : B) x = toHalf((float)((double)(mix(s) >> 11) * 0x1.0p-53 * 2 - 1));
        Buffer a, b;
        if (!c.deviceBuffer(2ull * n * n, a, err) || !c.deviceBuffer(2ull * n * n, b, err) ||
            !c.upload(a, A.data(), 2ull * n * n, err) || !c.upload(b, B.data(), 2ull * n * n, err)){ pass = false; break; }
        const Run fb = runKernel(c, "vk_gemm_tiled", 16, a, b, A, B, n, warmup, repeats);
        const Run fk = runKernel(c, "vk_gemm_tiled_skipk", 16, a, b, A, B, n, 0, 0);
        const bool fctl = fk.ran && !fk.check.pass();
        pass = pass && fb.ran && fb.check.pass() && fctl;
        std::string body = "{\"n\":" + std::to_string(n) + ",\"fallback\":" + runJson(fb, n) + ",\"control_fallback_skip_k\":" +
                           runJson(fk, n) + ",\"control_fallback_fails\":" + (fctl ? "true" : "false");
        if (coop){
            const Run tc = runKernel(c, "vk_gemm_coop", 32, a, b, A, B, n, warmup, repeats);
            const Run k1 = runKernel(c, "vk_gemm_coop_skipk", 32, a, b, A, B, n, 0, 0);
            const Run k2 = runKernel(c, "vk_gemm_coop_bcol", 32, a, b, A, B, n, 0, 0);
            const bool ctl = k1.ran && !k1.check.pass() && k2.ran && !k2.check.pass();
            pass = pass && tc.ran && tc.check.pass() && ctl;
            body += ",\"tensor\":" + runJson(tc, n) + ",\"control_skip_k\":" + runJson(k1, n) + ",\"control_layout\":" + runJson(k2, n) +
                    ",\"controls_fail\":" + (ctl ? "true" : "false");
            if (tc.ran && fb.ran && !tc.ms.empty() && !fb.ms.empty()){
                double lo = 0, hi = 0;
                bootstrap(fb.ms, tc.ms, lo, hi);
                body += ",\"speedup\":{\"median_ratio\":" + num(pct(fb.ms, 0.5) / pct(tc.ms, 0.5)) + ",\"ci95\":[" + num(lo) + "," + num(hi) + "]}";
            }
        }
        sj += (si ? ",\n  " : "") + body + "}";
        c.destroy(a); c.destroy(b);
    }
    const char* env = std::getenv("RAW_NATIVE_HW_DISABLE");
    const bool clean = c.validationErrors() == 0;
    pass = pass && clean && err.empty();
    out.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h1-7-bounds.json\",\n \"adapter\": \"" +
               jsonEscape(c.probe().adapter) + "\",\n \"backend\": \"" + jsonEscape(c.probe().backend) + "\",\n \"disable\": \"" +
               jsonEscape(env ? env : "") + "\",\n \"cooperative_matrix\": " + (coop ? "true" : "false") + ",\n \"path\": \"" +
               (coop ? "tensor cores and fallback" : "fallback only") + "\",\n \"error\": \"" + jsonEscape(err) + "\",\n \"warmup\": " +
               std::to_string(warmup) + ",\n \"repeats\": " + std::to_string(repeats) + ",\n \"sizes\": [\n  " + sj +
               "\n ],\n \"validation_errors\": " + std::to_string(c.validationErrors()) + ",\n \"pass\": " + (pass ? "true" : "false") + "\n}\n";
    out.code = pass ? 0 : 1;
    return out;
}
}  // namespace raw::rhi::hw
