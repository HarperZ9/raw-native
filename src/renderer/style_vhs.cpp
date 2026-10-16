// VHS reference: see raw/renderer/style_vhs.hpp. Operations are in the order of
// web/shaders/vhs/vhs.mjs and the filter builders of web/shaders/common.mjs.
#include "raw/renderer/style_vhs.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
std::vector<double> lowpassTaps(double cutoff, int half) {
    const int n = 2 * half + 1;
    std::vector<double> h(static_cast<std::size_t>(n));
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        const int m = i - half;
        const double x = 2 * cutoff * m;
        const double sinc = m == 0 ? 2 * cutoff : std::sin(kPi * x) / (kPi * m);
        const double w = 0.42 - 0.5 * std::cos((2 * kPi * i) / (n - 1)) + 0.08 * std::cos((4 * kPi * i) / (n - 1));
        h[std::size_t(i)] = sinc * w; sum += h[std::size_t(i)];
    }
    for (double& v : h) v /= sum;
    return h;
}
std::vector<double> gaussTaps(double cutoff) {
    const double sigma = 0.13251 / cutoff;
    const int r = int(std::ceil(4 * sigma));
    std::vector<double> h(static_cast<std::size_t>(2 * r + 1));
    double s = 0;
    for (int i = -r; i <= r; ++i) { const double q = i / sigma; h[std::size_t(i + r)] = std::exp(-0.5 * (q * q)); s += h[std::size_t(i + r)]; }
    for (double& v : h) v /= s;
    return h;
}
std::uint32_t u(long long v) { return std::uint32_t(v); }   // JavaScript's x >>> 0
template <class Get> double fir(const std::vector<double>& taps, Get get, double k) {
    const int h = int(taps.size() - 1) / 2;
    double s = 0;
    for (int j = -h; j <= h; ++j) s += taps[std::size_t(j + h)] * get(k + j);
    return s;
}
}  // namespace

const std::array<double, 9> kYiq = {0.299, 0.587, 0.114, 0.596, -0.274, -0.322, 0.211, -0.523, 0.312};
std::array<double, 9> vhsYiqInverse() {
    const auto& m = kYiq;
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g, det = a * A + b * B + c * C;
    return {A / det, -(b * i - c * h) / det, (b * f - c * e) / det, B / det, (a * i - c * g) / det, -(a * f - c * d) / det,
            C / det, -(a * h - b * g) / det, (a * e - b * d) / det};
}

VhsParams vhsPreset(const std::string& name) {
    VhsParams p;
    if (name == "sp-fresh") return p;
    if (name == "lp-worn") { p.lumaMHz = 2.5; p.chromaMHz = 0.45; p.lumaNoise = 0.022; p.chromaNoise = 0.018; p.jitter = 0.6; p.peaking = 0.4; p.dropouts = 0.012; p.chromaDelayNs = 220; p.phaseNoiseDeg = 3; return p; }
    if (name == "ep-rental") { p.lumaMHz = 2.2; p.chromaMHz = 0.4; p.lumaNoise = 0.03; p.chromaNoise = 0.028; p.jitter = 1.0; p.peaking = 0.45; p.dropouts = 0.02; p.chromaDelayNs = 260; p.phaseNoiseDeg = 4; p.generations = 3; p.tracking = 0.6; return p; }
    if (name == "thriller") { p.lumaMHz = 2.4; p.chromaMHz = 0.42; p.lumaNoise = 0.02; p.chromaNoise = 0.016; p.jitter = 0.7; p.peaking = 0.42; p.dropouts = 0.008; p.chromaDelayNs = 240; p.phaseNoiseDeg = 3.5; p.generations = 2; p.tracking = 0; return p; }
    throw std::invalid_argument("vhs: unknown preset " + name);
}

VhsPlan resolveVhs(const VhsParams& p, int srcW, int srcH) {
    VhsPlan plan;
    plan.p = p;
    plan.fs = 4 * p.fsc;
    plan.g = std::max(1, p.generations);
    plan.N = std::max(1, int(std::floor(srcW * plan.fs / p.pixelClockMHz + 0.5)));
    plan.lines = srcH; plan.srcW = srcW; plan.srcH = srcH;
    const double sg = std::sqrt(double(plan.g)), lumaMHz = p.lumaMHz / sg, chromaMHz = p.chromaMHz / sg;
    plan.luma = lowpassTaps(std::min(0.49, lumaMHz / plan.fs), 32);
    plan.peak = lowpassTaps(std::min(0.49, (lumaMHz * 0.45) / plan.fs), 32);
    plan.chroma = gaussTaps(chromaMHz / plan.fs);
    plan.lumaNoise = p.lumaNoise * sg; plan.chromaNoise = p.chromaNoise * sg;
    plan.delay = p.chromaDelayNs * 1e-3 * plan.fs;
    plan.phaseNoise = (p.phaseNoiseDeg * kPi) / 180 * sg;
    return plan;
}

double vhsGauss(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const double u1 = std::max(1e-7, hash3(a, b, c)), u2 = hash3(a, b, c + 7919u);
    return std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
}
double vhsTrackingBand(const VhsPlan& plan, int y, int f) {
    const double c = std::fmod(f * 3.7, double(plan.lines + 40)) - 20, d = std::abs(y - c) / 6;
    return plan.p.tracking * std::max(0.0, 1 - d * d);
}
double vhsLineShift(const VhsPlan& plan, int y, int f) {
    const VhsParams& p = plan.p;
    const int s = p.seed;
    double d = p.jitter * (0.6 * std::sin(y * 0.031 + f * 0.7 + s) + 0.4 * vhsGauss(u(y), u(f), u(11 + s)));
    const int fromBottom = plan.lines - 1 - y;
    if (fromBottom < p.headSwitchLines) { const double t = double(p.headSwitchLines - fromBottom) / p.headSwitchLines; d += p.headSkew * std::pow(t, 2); }
    if (p.tracking > 0) { const double band = vhsTrackingBand(plan, y, f); d += band * 12 * vhsGauss(u(y), u(f), u(23 + s)); }
    return d;
}

std::vector<float> vhsFrame(const VhsPlan& plan, const std::vector<float>& src, int f) {
    const int N = plan.N, lines = plan.lines, sw = plan.srcW, s = plan.p.seed;
    const std::size_t n = std::size_t(N) * lines;
    std::vector<float> sm(n * 4), tp(n * 4), sig(n * 4);
    auto held = [&](int y, int k, double out[3]) {
        const int kk = std::min(N - 1, std::max(0, k));
        const int x = std::min(sw - 1, std::max(0, int(std::floor(((kk + 0.5) * sw) / N))));
        const std::size_t i = (std::size_t(y) * sw + std::size_t(x)) * 4;
        const double r = src[i], g = src[i + 1], b = src[i + 2];
        for (int c = 0; c < 3; ++c) out[c] = kYiq[std::size_t(3 * c)] * r + kYiq[std::size_t(3 * c + 1)] * g + kYiq[std::size_t(3 * c + 2)] * b;
    };
    for (int y = 0; y < lines; ++y) {   // pass 1: the transport
        const double sh = vhsLineShift(plan, y, f);
        for (int k = 0; k < N; ++k) {
            const double kf = k - sh, k0 = std::floor(kf), t = kf - k0;
            double a[3], b[3];
            held(y, int(k0), a); held(y, int(k0) + 1, b);
            const std::size_t o = (std::size_t(y) * N + std::size_t(k)) * 4;
            for (int c = 0; c < 3; ++c) sm[o + std::size_t(c)] = float(a[c] * (1 - t) + b[c] * t);
            sm[o + 3] = 0;
        }
    }
    for (int y = 0; y < lines; ++y) {   // pass 2: the tape
        const double rot = plan.phaseNoise * vhsGauss(u(y), u(f), u(31 + s)), cr = std::cos(rot), sr = std::sin(rot);
        auto at = [&](double k, int c) {
            const double kf = std::min(double(N - 1), std::max(0.0, k)), k0 = std::floor(kf), t = kf - k0;
            const int k1 = std::min(N - 1, int(k0) + 1);
            return double(sm[(std::size_t(y) * N + std::size_t(k0)) * 4 + std::size_t(c)]) * (1 - t) + double(sm[(std::size_t(y) * N + std::size_t(k1)) * 4 + std::size_t(c)]) * t;
        };
        for (int k = 0; k < N; ++k) {
            const double Y = fir(plan.luma, [&](double kk) { return at(kk, 0); }, k), Yl = fir(plan.peak, [&](double kk) { return at(kk, 0); }, k);
            const long long gk = (long long)y * 4096 + k;
            const double nz = plan.lumaNoise * (vhsGauss(u(gk), u(f), u(41 + s)) - 0.5 * (vhsGauss(u(gk - 1), u(f), u(41 + s)) + vhsGauss(u(gk + 1), u(f), u(41 + s))));
            const double Yo = Y + plan.p.peaking * (Y - Yl) + nz;
            const double kd = k - plan.delay, I = fir(plan.chroma, [&](double kk) { return at(kk, 1); }, kd), Q = fir(plan.chroma, [&](double kk) { return at(kk, 2); }, kd);
            const double m = k / plan.chromaKnot, m0 = std::floor(m), t = m - m0;
            auto cn = [&](int c) { return plan.chromaNoise * ((1 - t) * vhsGauss(u(y), u((long long)m0), u(53 + c + s)) + t * vhsGauss(u(y), u((long long)m0 + 1), u(53 + c + s))); };
            const double band = plan.p.tracking > 0 ? vhsTrackingBand(plan, y, f) : 0, tn = band * 0.5 * vhsGauss(u(gk), u(f), u(61 + s));
            const std::size_t o = (std::size_t(y) * N + std::size_t(k)) * 4;
            tp[o] = float(Yo + tn); tp[o + 1] = float(I * cr - Q * sr + cn(0)); tp[o + 2] = float(I * sr + Q * cr + cn(1)); tp[o + 3] = 0;
        }
    }
    const std::array<double, 9> M = vhsYiqInverse();
    const double dropP = plan.p.dropouts * plan.g;
    for (int y = 0; y < lines; ++y) for (int k = 0; k < N; ++k) {   // pass 3: playback
        const int seg = k / 64;
        const bool hit = hash3(u(y), u(seg), u((long long)f * 131 + 71)) < dropP;
        const double run = hash3(u(y), u(seg), u((long long)f * 131 + 73)) * 64;
        const bool drop = hit && k - seg * 64 < run && y > 0;
        const std::size_t q = (std::size_t(drop ? y - 1 : y) * N + std::size_t(k)) * 4, o = (std::size_t(y) * N + std::size_t(k)) * 4;
        const double Y = tp[q], I = tp[q + 1], Q = tp[q + 2];
        for (int c = 0; c < 3; ++c) sig[o + std::size_t(c)] = float(M[std::size_t(3 * c)] * Y + M[std::size_t(3 * c + 1)] * I + M[std::size_t(3 * c + 2)] * Q);
        sig[o + 3] = drop ? 1.0f : 0.0f;
    }
    return sig;
}

std::vector<std::uint8_t> vhsDisplay(const VhsPlan& plan, const std::vector<float>& sig, int w, int h) {
    const int N = plan.N, lines = plan.lines;
    std::vector<std::uint8_t> o(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const int ly = std::min(lines - 1, int(std::floor(double(y) * lines / h)));
        const double kf = ((x + 0.5) * N) / w - 0.5;
        const int k0 = std::max(0, std::min(N - 1, int(std::floor(kf)))), k1 = std::min(N - 1, k0 + 1);
        const double t = std::min(1.0, std::max(0.0, kf - k0));
        const std::size_t p = (std::size_t(y) * w + std::size_t(x)) * 4;
        for (int c = 0; c < 3; ++c) {
            const double v = double(sig[(std::size_t(ly) * N + std::size_t(k0)) * 4 + std::size_t(c)]) * (1 - t) + double(sig[(std::size_t(ly) * N + std::size_t(k1)) * 4 + std::size_t(c)]) * t;
            o[p + std::size_t(c)] = std::uint8_t(std::floor(std::min(1.0, std::max(0.0, v)) * 255 + 0.5));
        }
        o[p + 3] = 255;
    }
    return o;
}

}  // namespace raw::style
