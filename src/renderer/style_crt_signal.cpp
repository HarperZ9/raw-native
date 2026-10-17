// The physical CRT's video signal and its parameter block, in the order of
// web/shaders/crt/signal.mjs and layout.mjs. See raw/renderer/style_crt.hpp.
#include "raw/renderer/style_crt.hpp"
#include "raw/renderer/style_spectral.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
const spectral::M3 kYiq = {0.299, 0.587, 0.114, 0.596, -0.274, -0.322, 0.211, -0.523, 0.312};
const spectral::M3 kYuv = {0.299, 0.587, 0.114, -0.14713, -0.28886, 0.436, 0.615, -0.51499, -0.10001};
double frac(double x) { return x - std::floor(x); }
double phase(const CrtPlan& plan, int k, int y, int f) { return 2 * kPi * (double(k & 3) / 4 + frac(y * plan.lineCyc) + frac(f * plan.frameCyc)); }
double palSign(int y) { return (y & 1) == 0 ? 1 : -1; }
struct Taps { const std::vector<float>& d; int o, h; double at(int j) const { return d[std::size_t(o + j + h)]; } };
Taps tapsOf(const CrtPlan& plan, CrtTap t) { return {plan.taps, plan.tapInfo[std::size_t(t)][0], plan.tapInfo[std::size_t(t)][1]}; }
double held(const CrtPlan& plan, const std::vector<float>& src, int y, int k, int c) {
    const int kk = std::min(plan.N - 1, std::max(0, k));
    const int x = std::min(plan.srcW - 1, int(std::floor(double((2 * kk + 1) * plan.srcW) / (2.0 * plan.N))));
    return src[(std::size_t(y) * plan.srcW + x) * 4 + std::size_t(c)];
}
std::array<double, 2> chromaOf(const CrtPlan& plan, const std::vector<float>& src, int y, int k) {
    const spectral::M3& m = plan.p.pal ? kYuv : kYiq;
    const double r = held(plan, src, y, k, 0), g = held(plan, src, y, k, 1), b = held(plan, src, y, k, 2);
    return {m[3] * r + m[4] * g + m[5] * b, m[6] * r + m[7] * g + m[8] * b};
}
std::vector<float> encode(const CrtPlan& plan, const std::vector<float>& src, int f) {
    const int N = plan.N, lines = plan.lines;
    std::vector<float> out(std::size_t(N) * lines * 4, 0.0f);
    const Taps E = tapsOf(plan, TapEnc), Rg = tapsOf(plan, TapRgb);
    for (int y = 0; y < lines; ++y) for (int k = 0; k < N; ++k) {
        const std::size_t i = (std::size_t(y) * N + k) * 4;
        if (plan.p.mode == 0) {
            for (int c = 0; c < 3; ++c) { double s = 0; for (int j = -Rg.h; j <= Rg.h; ++j) s += Rg.at(j) * held(plan, src, y, k + j, c); out[i + std::size_t(c)] = float(s); }
            continue;
        }
        const double r = held(plan, src, y, k, 0), g = held(plan, src, y, k, 1), b = held(plan, src, y, k, 2);
        double a = 0, q = 0;
        for (int j = -E.h; j <= E.h; ++j) { const auto cq = chromaOf(plan, src, y, k + j); a += E.at(j) * cq[0]; q += E.at(j) * cq[1]; }
        const double ph = phase(plan, k, y, f);
        const double c = plan.p.pal ? a * std::sin(ph) + palSign(y) * q * std::cos(ph) : a * std::cos(ph) + q * std::sin(ph);
        out[i] = float(0.299 * r + 0.587 * g + 0.114 * b); out[i + 1] = float(c);
    }
    return out;
}
std::vector<float> separate(const CrtPlan& plan, const std::vector<float>& enc) {
    const int N = plan.N, lines = plan.lines;
    std::vector<float> out(std::size_t(N) * lines * 4, 0.0f);
    const Taps S = tapsOf(plan, TapSep);
    auto comp = [&](int y, int k) { const std::size_t i = (std::size_t(y) * N + std::size_t(std::min(N - 1, std::max(0, k)))) * 4; return double(enc[i]) + double(enc[i + 1]); };
    const bool comb = plan.p.comb && !plan.p.pal;
    for (int y = 0; y < lines; ++y) for (int k = 0; k < N; ++k) {
        const std::size_t i = (std::size_t(y) * N + k) * 4;
        if (plan.p.mode == 1) { out[i] = enc[i]; out[i + 1] = enc[i + 1]; continue; }
        double yl;
        if (comb) { const int y2 = y > 0 ? y - 1 : std::min(1, lines - 1); yl = 0.5 * (comp(y, k) + comp(y2, k)); }
        else { yl = 0; for (int j = -S.h; j <= S.h; ++j) yl += S.at(j) * comp(y, k + j); }
        out[i] = float(yl); out[i + 1] = float(comp(y, k) - yl);
    }
    return out;
}
std::array<double, 2> demodLine(const CrtPlan& plan, const std::vector<float>& sep, int y, int k, int f) {
    const int N = plan.N;
    const Taps I = tapsOf(plan, TapI), Q = tapsOf(plan, TapQ);
    auto C = [&](int kk) { return double(sep[(std::size_t(y) * N + std::size_t(std::min(N - 1, std::max(0, kk)))) * 4 + 1]); };
    double a = 0, b = 0;
    for (int j = -I.h; j <= I.h; ++j) { const double ph = phase(plan, k + j, y, f); a += I.at(j) * 2 * C(k + j) * (plan.p.pal ? std::sin(ph) : std::cos(ph)); }
    for (int j = -Q.h; j <= Q.h; ++j) { const double ph = phase(plan, k + j, y, f); b += Q.at(j) * 2 * C(k + j) * (plan.p.pal ? palSign(y) * std::cos(ph) : std::sin(ph)); }
    return {a, b};
}
std::vector<float> demod(const CrtPlan& plan, const std::vector<float>& sep, int f) {
    const int N = plan.N, lines = plan.lines;
    std::vector<float> out(std::size_t(N) * lines * 4, 0.0f);
    const Taps L = tapsOf(plan, TapLum);
    const spectral::M3 M = spectral::inv3(plan.p.pal ? kYuv : kYiq);
    for (int y = 0; y < lines; ++y) for (int k = 0; k < N; ++k) {
        double Y = 0;
        for (int j = -L.h; j <= L.h; ++j) Y += L.at(j) * sep[(std::size_t(y) * N + std::size_t(std::min(N - 1, std::max(0, k + j)))) * 4];
        auto ab = demodLine(plan, sep, y, k, f);
        if (plan.p.pal && y > 0) { const auto ab2 = demodLine(plan, sep, y - 1, k, f); ab = {0.5 * (ab[0] + ab2[0]), 0.5 * (ab[1] + ab2[1])}; }
        const std::size_t i = (std::size_t(y) * N + k) * 4;
        out[i] = float(M[0] * Y + M[1] * ab[0] + M[2] * ab[1]); out[i + 1] = float(M[3] * Y + M[4] * ab[0] + M[5] * ab[1]); out[i + 2] = float(M[6] * Y + M[7] * ab[0] + M[8] * ab[1]);
    }
    return out;
}
double maskMeanOf(const CrtParams& m) {
    if (m.maskType == 1) return m.fill / 3;
    if (m.maskType == 2) return (m.fill / 3) * (m.slotHeight / (m.slotHeight + m.slotGap));
    if (m.maskType == 3) { const double d = m.pitch / std::sqrt(3.0), r = (m.fill * d) / 2; return (kPi * r * r) / ((std::sqrt(3.0) / 2) * m.pitch * m.pitch); }
    return 1;
}
}  // namespace

double crtMaskMean(const CrtParams& m) { return maskMeanOf(m); }

std::vector<float> crtSignal(const CrtPlan& plan, const std::vector<float>& src, int f) {
    const std::vector<float> enc = encode(plan, src, f);
    if (plan.p.mode == 0) return enc;
    return demod(plan, separate(plan, enc), f);
}

std::vector<float> crtPackParams(const CrtPlan& plan, int frame, double exposure) {
    const std::vector<double> d = crtParamBlock(plan, frame, exposure);
    std::vector<float> o(d.size());
    for (std::size_t i = 0; i < d.size(); ++i) o[i] = float(d[i]);
    return o;
}
std::vector<double> crtParamBlock(const CrtPlan& plan, int frame, double exposure) {
    std::vector<double> a;
    const CrtParams& p = plan.p;
    auto put = [&](double v) { a.push_back(v); };
    auto put3x3 = [&](const spectral::M3& m) { for (double v : m) put(v); };
    put(plan.srcW); put(plan.srcH); put(plan.N); put(plan.lines); put(p.mode); put(p.pal ? 1 : 0); put(p.comb && !p.pal ? 1 : 0);
    put(plan.lineCyc - std::floor(plan.lineCyc)); put(plan.frameCyc - std::floor(plan.frameCyc)); put(frame);
    for (const auto& t : plan.tapInfo) { put(t[0]); put(t[1]); }
    put3x3(p.pal ? kYuv : kYiq); put3x3(spectral::inv3(p.pal ? kYuv : kYiq));
    put(plan.outW); put(plan.outH); put(plan.viewW); put(plan.viewH); put(p.centre[0]); put(p.centre[1]); put(p.rx); put(p.ry); put(plan.distance);
    put(plan.faceW); put(plan.faceH); put(plan.rasterW); put(plan.rasterH); put(plan.lineMm); put(p.corner);
    put(p.contrast); put(p.brightness); put(p.gamma); put(p.spotLo); put(p.spotHi); put(p.spotExp);
    put(plan.screen.mono ? 0 : p.maskType); put(p.pitch); put(p.fill); put(p.slotHeight); put(p.slotGap);
    for (std::size_t i = 0; i < 2; ++i) put(i < p.dampers.size() ? p.dampers[i] : 0.0);
    put(std::min<std::size_t>(2, p.dampers.size())); put(p.damperWidth); put(p.mix); put(maskMeanOf(p));
    put(p.pixelLock && p.maskType != 3 ? 1 : 0); put(p.pitchPx);
    put(p.convR[0]); put(p.convR[1]); put(p.convB[0]); put(p.convB[1]); put(p.radial);
    put(plan.screen.mono ? 1 : 0); put(p.interlace ? 1 : 0);
    for (const auto& d : plan.decay) for (double v : d) put(v);
    put(plan.field);
    put(plan.halo.q); put(plan.halo.R); put(plan.halo.direct);
    put(std::ceil(double(plan.outW) / plan.halo.q)); put(std::ceil(double(plan.outH) / plan.halo.q));
    for (double v : plan.screen.matrix) put(v);
    put(0.2126); put(0.7152); put(0.0722);
    put(p.ambient); put(p.transmission); put(p.reflection); put(p.glassN); put(p.bezel[0]); put(p.bezel[1]); put(p.bezel[2]); put(exposure);
    return a;
}

}  // namespace raw::style
