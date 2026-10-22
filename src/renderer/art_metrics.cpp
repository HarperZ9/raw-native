// The stylised track's measures: see raw/renderer/art_metrics.hpp.
#include "raw/renderer/art_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace raw::art {

std::uint8_t encode8(double v) {
    const double l = std::clamp(v, 0.0, 1.0);
    const double s = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
    return std::uint8_t(std::floor(s * 255 + 0.5));
}

std::vector<std::uint8_t> codes8(const std::vector<float>& rgba) {
    std::vector<std::uint8_t> o(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); ++i) o[i] = (i & 3) == 3 ? 255 : encode8(rgba[i]);
    return o;
}

double instability(const std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& outPrev,
                   const std::vector<std::uint8_t>& ref, const std::vector<std::uint8_t>& refPrev,
                   const std::vector<float>& mv, const std::vector<std::uint8_t>& valid, int w, int h) {
    double sum = 0;
    long n = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
        if (!valid[p]) continue;
        const int qx = std::clamp(int(std::floor(x - double(mv[p * 2]) + 0.5)), 0, w - 1);
        const int qy = std::clamp(int(std::floor(y - double(mv[p * 2 + 1]) + 0.5)), 0, h - 1);
        const std::size_t q = std::size_t(qy) * std::size_t(w) + std::size_t(qx);
        for (int c = 0; c < 3; ++c) {
            const double o = (double(out[p * 4 + std::size_t(c)]) - outPrev[q * 4 + std::size_t(c)]) / 255.0;
            const double r = (double(ref[p * 4 + std::size_t(c)]) - refPrev[q * 4 + std::size_t(c)]) / 255.0;
            sum += std::abs(o - r);
        }
        ++n;
    }
    return n > 0 ? sum / (3.0 * double(n)) : -1.0;
}

namespace {
std::uint64_t splitmix(std::uint64_t& s) {
    std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
double percentile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    const double pos = q * double(v.size() - 1);
    const std::size_t i = std::size_t(std::floor(pos));
    const double t = pos - double(i);
    return i + 1 < v.size() ? v[i] * (1 - t) + v[i + 1] * t : v[i];
}
}  // namespace

Paired pairedBootstrap(const std::vector<double>& a, const std::vector<double>& b, std::uint64_t seed, int resamples) {
    Paired r;
    r.frames = int(a.size()); r.seed = seed;
    if (a.empty() || a.size() != b.size()) return r;
    const std::size_t n = a.size();
    for (std::size_t i = 0; i < n; ++i) { r.meanA += a[i] / double(n); r.meanB += b[i] / double(n); }
    r.diff = r.meanA - r.meanB;
    r.reduction = r.meanB > 0 ? 1 - r.meanA / r.meanB : 0;
    std::vector<double> diffs, reds;
    std::uint64_t s = seed;
    for (int k = 0; k < resamples; ++k) {
        double sa = 0, sb = 0;
        for (std::size_t i = 0; i < n; ++i) { const std::size_t j = std::size_t(splitmix(s) % n); sa += a[j]; sb += b[j]; }
        diffs.push_back((sa - sb) / double(n));
        reds.push_back(sb > 0 ? 1 - sa / sb : 0);
    }
    r.diffLo = percentile(diffs, 0.025); r.diffHi = percentile(diffs, 0.975);
    r.redLo = percentile(reds, 0.025); r.redHi = percentile(reds, 0.975);
    return r;
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
    return h;
}
std::uint64_t fnv1a(const std::vector<float>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (float f : v) {
        std::uint32_t b;
        std::memcpy(&b, &f, 4);
        for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255u; h *= 1099511628211ull; }
    }
    return h;
}

}  // namespace raw::art
