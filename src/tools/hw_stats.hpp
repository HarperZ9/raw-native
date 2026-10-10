#pragma once
// Shared timing statistics for the HW tools (private to src/tools/hw_*.cpp): percentiles,
// a JSON summary and the bootstrap interval of a ratio of medians (PLAN.md speed rules).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace raw::hwstats {
inline std::string num(double v){ char b[48]; std::snprintf(b, sizeof b, "%.6g", v); return b; }
inline double pct(std::vector<double> v, double p){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double k = p * (double)(v.size() - 1);
    const size_t i = (size_t)k;
    return i + 1 < v.size() ? v[i] + (k - (double)i) * (v[i + 1] - v[i]) : v[i];
}
inline std::string summary(const std::vector<double>& ms){
    if (ms.empty()) return "null";
    return "{\"n\":" + std::to_string(ms.size()) + ",\"median_ms\":" + num(pct(ms, 0.5)) + ",\"p5_ms\":" + num(pct(ms, 0.05)) +
           ",\"p95_ms\":" + num(pct(ms, 0.95)) + ",\"p99_ms\":" + num(pct(ms, 0.99)) + ",\"iqr_ms\":" + num(pct(ms, 0.75) - pct(ms, 0.25)) + "}";
}
// median(a) / median(b) and its 95% bootstrap interval over 10,000 resamples.
inline void ratioInterval(const std::vector<double>& a, const std::vector<double>& b, double& ratio, double& lo, double& hi){
    uint64_t s = 0xB007ull;
    auto next = [&]{ uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); };
    std::vector<double> rs, ra(a.size()), rb(b.size());
    for (int it = 0; it < 10000; ++it){
        for (auto& x : ra) x = a[next() % a.size()];
        for (auto& x : rb) x = b[next() % b.size()];
        rs.push_back(pct(ra, 0.5) / pct(rb, 0.5));
    }
    ratio = pct(a, 0.5) / pct(b, 0.5); lo = pct(rs, 0.025); hi = pct(rs, 0.975);
}
}
