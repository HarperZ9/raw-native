// One-ray area-light visibility and the Penumbra Yard: see raw/renderer/style_rtvis.hpp.
// Operations are in the order of web/shaders/ditherlight/rtvis.mjs and fixtures/penumbra.mjs.
#include "raw/renderer/style_rtvis.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
int binom(int n, int k) { double c = 1; for (int i = 0; i < k; ++i) c = (c * (n - i)) / (i + 1); return int(c); }
double hashU(int x, int y, int f, int k) {
    const std::uint64_t s = std::uint64_t(x) * 374761393ull + std::uint64_t(y) * 668265263ull + std::uint64_t(f) * 2147483647ull + std::uint64_t(k) * 1274126177ull;
    std::uint32_t h = std::uint32_t(s);
    h = (h ^ (h >> 13)) * 1274126177u;
    return double(h ^ (h >> 16)) / 4294967296.0;
}
}  // namespace

int rtvisBitrev6(int r) { int o = 0; for (int b = 0; b < 6; ++b) o |= ((r >> b) & 1) << (5 - b); return o; }
int rtvisPascal6(int r) {
    int o = 0;
    for (int i = 0; i < 6; ++i) {
        int s = 0;
        for (int j = i; j < 6; ++j) if ((binom(j, i) & 1) && ((r >> j) & 1)) s ^= 1;
        o |= s << (5 - i);
    }
    return o;
}
int rtvisBayer8Rank(int x, int y) {
    x &= 7; y &= 7;
    const int v = x ^ y;
    return ((v & 1) << 5) | ((y & 1) << 4) | (((v >> 1) & 1) << 3) | (((y >> 1) & 1) << 2) | (((v >> 2) & 1) << 1) | ((y >> 2) & 1);
}
std::array<double, 2> rtvisConcentric(double u, double v) {
    const double a = 2 * u - 1, b = 2 * v - 1;
    if (a == 0 && b == 0) return {0, 0};
    double r, phi;
    if (std::abs(a) > std::abs(b)) { r = a; phi = (kPi / 4) * (b / a); } else { r = b; phi = kPi / 2 - (kPi / 4) * (a / b); }
    return {r * std::cos(phi), r * std::sin(phi)};
}
RV3 rtvisLightPoint(const RV3& C, double R, const RV3& P, double u, double v) {
    const RV3 w{C[0] - P[0], C[1] - P[1], C[2] - P[2]};
    const double l = std::hypot(w[0], w[1], w[2]);
    const RV3 n{w[0] / l, w[1] / l, w[2] / l};
    const RV3 a = std::abs(n[0]) < 0.9 ? RV3{1, 0, 0} : RV3{0, 1, 0};
    RV3 t{n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0]};
    const double tl = std::hypot(t[0], t[1], t[2]);
    for (double& c : t) c = c / tl;
    const RV3 b{n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]};
    const auto xy = rtvisConcentric(u, v);
    return {C[0] + R * (xy[0] * t[0] + xy[1] * b[0]), C[1] + R * (xy[0] * t[1] + xy[1] * b[1]), C[2] + R * (xy[0] * t[2] + xy[1] * b[2])};
}
YardBlocker yardBlocker(const std::string& name) {
    YardBlocker b;
    if (name == "wall-x") { b.lo = {-3, 0, 0.775}; b.hi = {3, 1.2, 0.825}; return b; }
    if (name == "wall-z") { b.lo = {0.775, 0, -3}; b.hi = {0.825, 1.2, 3}; return b; }
    if (name == "sphere") { b.box = false; b.c = {0.6, 1.0, 0.6}; b.r = 0.5; return b; }
    throw std::invalid_argument("penumbra yard: unknown blocker " + name);
}
bool yardOccluded(const YardBlocker& b, const RV3& P, const RV3& Q) {
    if (b.box) {
        double t0 = 0, t1 = 1;
        for (int k = 0; k < 3; ++k) {
            const double d = Q[std::size_t(k)] - P[std::size_t(k)];
            if (std::abs(d) < 1e-12) { if (P[std::size_t(k)] < b.lo[std::size_t(k)] || P[std::size_t(k)] > b.hi[std::size_t(k)]) return false; continue; }
            double a = (b.lo[std::size_t(k)] - P[std::size_t(k)]) / d, c = (b.hi[std::size_t(k)] - P[std::size_t(k)]) / d;
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a); t1 = std::min(t1, c);
            if (t0 > t1) return false;
        }
        return true;
    }
    const RV3 d{Q[0] - P[0], Q[1] - P[1], Q[2] - P[2]}, o{P[0] - b.c[0], P[1] - b.c[1], P[2] - b.c[2]};
    const double A = d[0] * d[0] + d[1] * d[1] + d[2] * d[2], B = 2 * (o[0] * d[0] + o[1] * d[1] + o[2] * d[2]), C = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - b.r * b.r;
    const double disc = B * B - 4 * A * C;
    if (disc < 0) return false;
    const double s = std::sqrt(disc), t0 = (-B - s) / (2 * A), t1 = (-B + s) / (2 * A);
    return t1 >= 0 && t0 <= 1;
}
RV3 yardPoint(int x, int y) { return {-8.0 / 2 + ((x + 0.5) * 8) / 256, 0, -8.0 / 2 + ((y + 0.5) * 8) / 256}; }
double yardTone(const RV3& P, const YardLight& L, double V) {
    const RV3 v{L.pos[0] - P[0], L.pos[1] - P[1], L.pos[2] - P[2]};
    const double d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2], c = v[1] / std::sqrt(d2);
    return std::min(1.0, 0.8 * (0.1 + (9 * std::max(0.0, c) * V) / d2));
}
std::vector<RV3> rtvisSamples(const std::string& method, const YardLight& L, const RV3& P, int x, int y, int f) {
    std::vector<std::array<double, 2>> S;
    if (method == "net1") { const int r = rtvisBayer8Rank(x, y); S.push_back({(rtvisBitrev6(r) + 0.5) / 64, (rtvisPascal6(r) + 0.5) / 64}); }
    else if (method == "rank1") { const int r = rtvisBayer8Rank(x, y); S.push_back({((r & 7) + 0.5) / 8, ((r >> 3) + 0.5) / 8}); }
    else if (method == "white1") S.push_back({hashU(x, y, f, 1), hashU(x, y, f, 2)});
    else if (method == "ref64") { for (int j = 0; j < 8; ++j) for (int i = 0; i < 8; ++i) S.push_back({(i + 0.5) / 8, (j + 0.5) / 8}); }
    else throw std::invalid_argument("rtvis: unknown method " + method);
    std::vector<RV3> Q;
    for (const auto& uv : S) Q.push_back(rtvisLightPoint(L.pos, L.radius, P, uv[0], uv[1]));
    return Q;
}
double rtvisVisibility(const std::string& method, const YardLight& L, const RV3& P, int x, int y, int f, const YardBlocker& b) {
    const std::vector<RV3> Q = rtvisSamples(method, L, P, x, y, f);
    int n = 0;
    for (const RV3& q : Q) if (!yardOccluded(b, P, q)) n++;
    return double(n) / double(Q.size());
}
double yardOcclusionMargin(const YardBlocker& b, const RV3& P, const RV3& Q) {
    double m = 1e300;
    if (b.box) {
        double t0 = 0, t1 = 1;
        for (int k = 0; k < 3; ++k) {
            const double d = Q[std::size_t(k)] - P[std::size_t(k)];
            if (std::abs(d) < 1e-12) { m = std::min({m, std::abs(P[std::size_t(k)] - b.lo[std::size_t(k)]), std::abs(P[std::size_t(k)] - b.hi[std::size_t(k)])}); continue; }
            double a = (b.lo[std::size_t(k)] - P[std::size_t(k)]) / d, c = (b.hi[std::size_t(k)] - P[std::size_t(k)]) / d;
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a); t1 = std::min(t1, c);
            m = std::min(m, std::abs(t1 - t0));
            if (t0 > t1) return m;
        }
        return m;
    }
    const RV3 d{Q[0] - P[0], Q[1] - P[1], Q[2] - P[2]}, o{P[0] - b.c[0], P[1] - b.c[1], P[2] - b.c[2]};
    const double A = d[0] * d[0] + d[1] * d[1] + d[2] * d[2], B = 2 * (o[0] * d[0] + o[1] * d[1] + o[2] * d[2]), C = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - b.r * b.r;
    const double disc = B * B - 4 * A * C;
    if (disc < 0) return std::abs(disc);
    const double s = std::sqrt(disc), t0 = (-B - s) / (2 * A), t1 = (-B + s) / (2 * A);
    return std::min({std::abs(disc), std::abs(t1), std::abs(1 - t0)});
}

}  // namespace raw::style
