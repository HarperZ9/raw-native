// The lens suite's plan: bin weights, coating, iris and starburst, the paraxial ghost table and
// resolveLens. See raw/renderer/style_lens.hpp; operations in the order of
// web/shaders/lens/spectrum.mjs, coating.mjs, aperture.mjs, prescription.mjs and plan.mjs.
#include "raw/renderer/style_lens.hpp"
#include "raw/renderer/style_spectral.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kLambdaD = 587.6;
double binNm(int l) { return 420 + 40 * l; }
// Sellmeier glasses from the SCHOTT catalogue (lens/spectrum.mjs GLASS): N-BK7 crown, N-SF2 flint.
struct Glass { double B[3], C[3]; };
constexpr Glass kCrown{{1.03961212, 0.231792344, 1.01046945}, {0.00600069867, 0.0200179144, 103.560653}};
constexpr Glass kFlint{{1.47343127, 0.163681849, 1.36920899}, {0.0109019098, 0.0585683687, 127.404933}};
constexpr Glass kAir{{0, 0, 0}, {0, 0, 0}};
double glassIndex(const Glass& g, double nm) {
    const double l2 = (nm / 1000) * (nm / 1000);
    double s = 1;
    for (int i = 0; i < 3; ++i) s += (g.B[i] * l2) / (l2 - g.C[i]);
    return std::sqrt(s);
}
struct Surface { double R, t; Glass glass; };
constexpr Surface kLens[6] = {{14.2, 3.5, kCrown}, {-288.4, 4.0, kAir}, {-29.7, 1.5, kFlint}, {43.9, 2.4, kAir}, {80.1, 3.0, kCrown}, {-42.8, 0, kAir}};
constexpr int kStopAfter = 3, kN = 6;
constexpr double kStopAt = 1.2;
using Ray = std::array<double, 2>;
double idx(int k, double nm) { return k < 0 ? 1 : glassIndex(kLens[k].glass, nm); }
double Cv(int k) { return 1 / kLens[k].R; }
Ray Tr(const Ray& r, double d, double n) { return {r[0] + (d * r[1]) / n, r[1]}; }
Ray Sf(const Ray& r, double c, double n, double n2) { return {r[0], r[1] - r[0] * (n2 - n) * c}; }
double toStop(double nm, Ray r) {
    for (int k = 0; k <= kStopAfter; ++k) { r = Sf(r, Cv(k), idx(k - 1, nm), idx(k, nm)); r = Tr(r, k == kStopAfter ? kStopAt : kLens[k].t, idx(k, nm)); }
    return r[0];
}
Ray forward(double nm, Ray r, int from, double sensor) {
    for (int k = from; k < kN; ++k) { r = Sf(r, Cv(k), idx(k - 1, nm), idx(k, nm)); r = Tr(r, k == kN - 1 ? sensor : kLens[k].t, idx(k, nm)); }
    return r;
}
Ray ghostPath(double nm, Ray r, int i, int j, double sensor) {
    for (int k = 0; k < i; ++k) { r = Sf(r, Cv(k), idx(k - 1, nm), idx(k, nm)); r = Tr(r, kLens[k].t, idx(k, nm)); }
    const double ni = idx(i - 1, nm);
    r = Sf(r, Cv(i), ni, -ni);
    for (int k = i - 1; k > j; --k) { r = Tr(r, -kLens[k].t, -idx(k, nm)); r = Sf(r, Cv(k), -idx(k, nm), -idx(k - 1, nm)); }
    r = Tr(r, -kLens[j].t, -idx(j, nm));
    const double nj = idx(j, nm);
    r = Sf(r, Cv(j), -nj, nj);
    r = Tr(r, kLens[j].t, nj);
    return forward(nm, r, j + 1, sensor);
}
double edgeOf(double phi, const LensIris& iris) {
    const int N = iris.blades;
    const double seg = (2 * kPi) / N;
    double a = std::fmod(phi - iris.rotation, seg);
    if (a < 0) a += seg;
    const double poly = std::cos(kPi / N) / std::cos(a - kPi / N);
    return poly + (1 - poly) * iris.roundness;
}
void fft(std::vector<double>& re, std::vector<double>& im) {
    const int n = int(re.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[std::size_t(i)], re[std::size_t(j)]); std::swap(im[std::size_t(i)], im[std::size_t(j)]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = (-2 * kPi) / len, wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                const std::size_t a = std::size_t(i + k), b = a + std::size_t(len / 2);
                const double tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}
std::vector<double> psfMono(const LensIris& iris, int n, double rad) {
    const std::size_t N2 = std::size_t(n) * n;
    std::vector<double> re(N2, 0.0), im(N2, 0.0);
    const double c = n / 2.0;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
        int cov = 0;
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
            const double u = (x + (sx + 0.5) / 4 - c) / rad, v = (y + (sy + 0.5) / 4 - c) / rad, r = std::hypot(u, v);
            if (r <= edgeOf(std::atan2(v, u), iris)) cov++;
        }
        re[std::size_t(y) * n + std::size_t(x)] = cov / 16.0;
    }
    std::vector<double> rr(static_cast<std::size_t>(n)), ri(static_cast<std::size_t>(n));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) { rr[std::size_t(x)] = re[std::size_t(y) * n + std::size_t(x)]; ri[std::size_t(x)] = im[std::size_t(y) * n + std::size_t(x)]; }
        fft(rr, ri);
        for (int x = 0; x < n; ++x) { re[std::size_t(y) * n + std::size_t(x)] = rr[std::size_t(x)]; im[std::size_t(y) * n + std::size_t(x)] = ri[std::size_t(x)]; }
    }
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y) { rr[std::size_t(y)] = re[std::size_t(y) * n + std::size_t(x)]; ri[std::size_t(y)] = im[std::size_t(y) * n + std::size_t(x)]; }
        fft(rr, ri);
        for (int y = 0; y < n; ++y) { re[std::size_t(y) * n + std::size_t(x)] = rr[std::size_t(y)]; im[std::size_t(y) * n + std::size_t(x)] = ri[std::size_t(y)]; }
    }
    std::vector<double> out(N2);
    double s = 0;
    const int ci = n / 2;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
        const std::size_t q = std::size_t((y + ci) % n) * n + std::size_t((x + ci) % n);
        const double p = re[q] * re[q] + im[q] * im[q];
        out[std::size_t(y) * n + std::size_t(x)] = p; s += p;
    }
    for (double& v : out) v /= s;
    return out;
}
double bilinearT(const std::vector<double>& t, int n, double x, double y) {
    if (x < 0 || y < 0 || x > n - 1 || y > n - 1) return 0;
    const int x0 = std::min(n - 2, int(std::floor(x))), y0 = std::min(n - 2, int(std::floor(y)));
    const double fx = x - x0, fy = y - y0;
    const std::size_t i = std::size_t(y0) * n + std::size_t(x0), nn = std::size_t(n);
    return (t[i] * (1 - fx) + t[i + 1] * fx) * (1 - fy) + (t[i + nn] * (1 - fx) + t[i + nn + 1] * fx) * fy;
}
std::array<double, 2> fresnel(double n0, double n1, double c0, double c1) {
    return {(n0 * c0 - n1 * c1) / (n0 * c0 + n1 * c1), (n1 * c0 - n0 * c1) / (n1 * c0 + n0 * c1)};
}
double airy(double a, double b, double d) {
    const double c = std::cos(d), s = std::sin(d);
    const double nr = a + b * c, ni = b * s, dr = 1 + a * b * c, di = a * b * s;
    return (nr * nr + ni * ni) / (dr * dr + di * di);
}
}  // namespace

LensBin3 lensBinRgb() {
    using namespace spectral;
    const M3 X = xyzToRec709();
    LensBin3 raw{};
    for (int l = 0; l < kLensBins; ++l) {
        const double c = binNm(l);
        T3 xyz{0, 0, 0};
        for (double wl = c - 20; wl < c + 20; wl++) { const T3 v = cmf(wl + 0.5); xyz[0] += v[0]; xyz[1] += v[1]; xyz[2] += v[2]; }
        const T3 rgb = apply3(X, xyz);
        for (int k = 0; k < 3; ++k) raw[std::size_t(l)][std::size_t(k)] = std::max(0.0, rgb[std::size_t(k)]);
    }
    std::array<double, 3> sum{0, 0, 0};
    for (int k = 0; k < 3; ++k) for (int l = 0; l < kLensBins; ++l) sum[std::size_t(k)] = sum[std::size_t(k)] + raw[std::size_t(l)][std::size_t(k)];
    for (auto& w : raw) for (int k = 0; k < 3; ++k) w[std::size_t(k)] = w[std::size_t(k)] / sum[std::size_t(k)];
    return raw;
}

LensGhostTable lensGhostTable() {
    LensGhostTable gt;
    const Ray r = forward(kLambdaD, {1, 0}, 0, 0);
    gt.f = -1 / r[1]; gt.bfd = -r[0] / r[1];
    const double a = toStop(kLambdaD, {1, 0}), b = toStop(kLambdaD, {0, 1}), u0 = 1e-3;
    const double yImg = forward(kLambdaD, {-(b * u0) / a, u0}, 0, gt.bfd)[0];
    for (int i = 1; i < kN; ++i) for (int j = 0; j < i; ++j) {
        std::array<double, kLensBins> m{}, rho{};
        for (int l = 0; l < kLensBins; ++l) {
            const double nm = binNm(l), al = toStop(nm, {1, 0}), bl = toStop(nm, {0, 1});
            const double chief = ghostPath(nm, {-(bl * u0) / al, u0}, i, j, gt.bfd)[0];
            const double marg = ghostPath(nm, {1, 0}, i, j, gt.bfd)[0];
            m[std::size_t(l)] = chief / yImg; rho[std::size_t(l)] = marg;
        }
        gt.m.push_back(m); gt.rho.push_back(rho);
    }
    return gt;
}

double lensFilmReflectance(double nm, double theta, const LensCoating& coat) {
    const double n0 = 1, n1 = coat.n1, ns = coat.ns, d = coat.quarterAt / (4 * n1);
    const double s0 = std::sin(theta), c0 = std::cos(theta);
    if (d <= 0 || coat.n1 <= 0) {
        const double s2 = s0 / ns, c2 = std::sqrt(1 - s2 * s2);
        const auto r = fresnel(1, ns, c0, c2);
        return 0.5 * (r[0] * r[0] + r[1] * r[1]);
    }
    const double s1 = (n0 * s0) / n1, c1 = std::sqrt(1 - s1 * s1), s2 = (n0 * s0) / ns, c2 = std::sqrt(1 - s2 * s2);
    const auto r01 = fresnel(n0, n1, c0, c1), r12 = fresnel(n1, ns, c1, c2);
    const double delta = (4 * kPi * n1 * d * c1) / nm;
    return 0.5 * (airy(r01[0], r12[0], delta) + airy(r01[1], r12[1], delta));
}

double lensShapeArea(const LensIris& iris) {
    const int steps = 4096;
    double s = 0;
    for (int k = 0; k < steps; ++k) { const double e = edgeOf(((k + 0.5) * 2 * kPi) / steps, iris); s += e * e; }
    return (0.5 * s * 2 * kPi) / steps;
}

LensStar lensStarburst(const LensIris& iris) {
    const int n = 256, M = 129;
    const double rad = 24, core = 6, lambdaRef = 560, c = n / 2.0, h = (M - 1) / 2.0;
    const std::vector<double> p = psfMono(iris, n, rad);
    const LensBin3 bin = lensBinRgb();
    LensStar st;
    st.M = M;
    st.data.assign(std::size_t(M) * M * 3, 0.0);
    for (int y = 0; y < M; ++y) for (int x = 0; x < M; ++x) {
        const double dx = x - h, dy = y - h, win = 1 - std::exp(-(dx * dx + dy * dy) / (core * core));
        for (int l = 0; l < kLensBins; ++l) {
            const double s = lambdaRef / binNm(l), v = bilinearT(p, n, c + dx * s, c + dy * s) * s * s * win;
            for (int k = 0; k < 3; ++k) st.data[(std::size_t(y) * M + std::size_t(x)) * 3 + std::size_t(k)] += bin[std::size_t(l)][std::size_t(k)] * v;
        }
    }
    double sum[3] = {0, 0, 0};
    for (std::size_t i = 0; i < std::size_t(M) * M; ++i) for (int k = 0; k < 3; ++k) sum[k] += st.data[i * 3 + std::size_t(k)];
    for (std::size_t i = 0; i < std::size_t(M) * M; ++i) for (int k = 0; k < 3; ++k) st.data[i * 3 + std::size_t(k)] /= sum[k];
    return st;
}

LensPlan resolveLens(const LensParams& in, int W, int H) {
    LensPlan plan;
    plan.p = in;
    LensParams& p = plan.p;
    plan.W = W; plan.H = H;
    plan.D = std::hypot(double(W), double(H)) / 2; plan.cx = W / 2.0; plan.cy = H / 2.0;
    const double k = H / p.refHeight;
    p.streakLenPx *= k; p.streakThickPx *= k; p.starCellPxPerStop *= k; p.haloRadiusPx *= k; p.minGhostPx *= k;
    p.streakThickPx = std::max(0.7, p.streakThickPx);
    static const LensGhostTable gt = lensGhostTable();
    plan.tanHalf = p.sensorHalfDiagMm / gt.f;
    const double pxPerRho = plan.D / (2 * p.fNumber * plan.tanHalf), area1 = lensShapeArea(p.iris);
    for (std::size_t g = 0; g < gt.m.size(); ++g) {
        std::array<std::array<double, 4>, kLensBins> row{};
        for (int l = 0; l < kLensBins; ++l) {
            const double rho = gt.rho[g][std::size_t(l)], r = std::max(p.minGhostPx, std::abs(rho) * pxPerRho);
            row[std::size_t(l)] = {gt.m[g][std::size_t(l)], r, p.iris.rotation + (rho < 0 ? kPi : 0), p.squeeze / (area1 * r * r)};
        }
        plan.ghosts.push_back(row);
    }
    double rs = 0;
    for (int l = 0; l < kLensBins; ++l) { plan.R0[std::size_t(l)] = lensFilmReflectance(binNm(l), 0, p.coating); rs = rs + plan.R0[std::size_t(l)]; }
    plan.Rbar = rs / kLensBins;
    const double nd = glassIndex(kCrown, kLambdaD);
    for (int l = 0; l < kLensBins; ++l) {
        const double nm = binNm(l), depth = 1 / (1 + std::exp(-(nm - 600) / 25));
        plan.halo[std::size_t(l)] = {p.haloGain * depth, p.haloRadiusPx * (0.5 + depth)};
        plan.caScale[std::size_t(l)] = 1 + (p.caAmount * (glassIndex(kCrown, nm) - nd)) / (nd - 1);
    }
    plan.star = lensStarburst(p.iris);
    plan.starCell = p.starCellPxPerStop * p.fNumber;
    plan.binRgb = lensBinRgb();
    const double ng = double(gt.m.size());
    for (int c = 0; c < 3; ++c) {
        double s = p.starGain;
        for (int l = 0; l < kLensBins; ++l) {
            const double w = plan.binRgb[std::size_t(l)][std::size_t(c)], R0 = plan.R0[std::size_t(l)];
            s += w * (ng * p.ghostGain * R0 * R0 + (p.streakGain * R0) / plan.Rbar + plan.halo[std::size_t(l)][0]);
        }
        plan.phi[std::size_t(c)] = s;
    }
    return plan;
}

}  // namespace raw::style
