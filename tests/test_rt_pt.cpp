// The path tracer's CPU checks (evidence/rt-r2-bounds.json): P0 BSDF consistency (addendum 1)
// with its control, the furnace on the CPU reference (P1's bound, applied to the reference),
// and thread-count determinism.
//   test_rt_pt [out.json]
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/core/parallel.hpp"
#include "../src/renderer/rt_pt_math.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
using namespace raw;
using namespace raw::rt;

namespace {
std::string js;
void add(const std::string& s) { js += (js.empty() ? "  " : ",\n  ") + s; }
int hw() { return int(std::max(1u, std::thread::hardware_concurrency())); }
struct Rng {
    std::uint64_t s;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); }
};
struct Est { double mean, se; };
Est meanSe(const std::vector<double>& v) {
    double s = 0, s2 = 0;
    for (double x : v) { s += x; s2 += x * x; }
    const double n = double(v.size()), m = s / n;
    return {m, std::sqrt(std::max(0.0, (s2 / n - m * m)) / (n - 1))};
}
// The independent reference of P0 (method note 1): the classic NDF sampling of h (tan^2 theta =
// a^2 u / (1 - u)) mixed one-sample with cosine sampling, weighted by the mixture's own pdf.
std::vector<double> ndfMixture(const pt::Bsdf& m, Vec3 wo, Rng& r, int n) {
    const float al = std::max(1e-4f, m.rough * m.rough), ps = pt::lobeProb(m);
    std::vector<double> out(std::size_t(n), 0.0);
    for (double& v : out) {
        Vec3 wi;
        const float pick = r.next(), u1 = r.next(), u2 = r.next();
        if (pick < ps) {
            const float t2 = al * al * u1 / (1.0f - u1), ct = 1.0f / std::sqrt(1.0f + t2), st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
            const Vec3 h{st * std::cos(6.2831853f * u2), st * std::sin(6.2831853f * u2), ct};
            wi = h * (2.0f * dot(wo, h)) - wo;
        } else {
            const float rr = std::sqrt(u1), ph = 6.2831853f * u2;
            wi = {rr * std::cos(ph), rr * std::sin(ph), std::sqrt(std::max(0.0f, 1.0f - u1))};
        }
        if (wi.z <= 0.0f) continue;
        const pt::Eval e = pt::evalBsdf(m, wo, wi);
        const Vec3 h = normalize(wo + wi);
        const float pdf = (1.0f - ps) * wi.z / pt::kPi + ps * pt::ggxD(al * al, h.z) * h.z / (4.0f * std::fabs(dot(wo, h)));
        if (pdf > 0) v = pt::luminance(e.fcos) / pdf;
    }
    return out;
}
// P0: directional albedo (luminance) by the tracer's sampling against the NDF-mixture reference;
// the cosine-weighted estimate of the first run is reported beside it.
void p0() {
    constexpr int kCases = 400, kImp = 65536, kRef = 262144;
    std::vector<double> ratio(kCases), ctlRatio(kCases), cosRatio(kCases);
    parallelRows(kCases, hw(), [&](int c) {
        Rng r{0x9000ULL + std::uint64_t(c) * 7919ULL};
        const pt::Bsdf m{{0.05f + 0.95f * r.next(), 0.05f + 0.95f * r.next(), 0.05f + 0.95f * r.next()},
                         0.05f + 0.95f * r.next(), r.next(), r.next()};
        const float mu = 0.05f + 0.95f * r.next(), phi = 6.2831853f * r.next(), sn = std::sqrt(1.0f - mu * mu);
        const Vec3 wo{sn * std::cos(phi), sn * std::sin(phi), mu};
        std::vector<double> a(kImp, 0.0), ac(kImp, 0.0), b(kRef, 0.0);
        for (int k = 0; k < kImp; ++k) {
            Vec3 wi;
            if (!pt::sampleBsdf(m, wo, r.next(), r.next(), r.next(), wi)) continue;
            const pt::Eval e = pt::evalBsdf(m, wo, wi), en = pt::evalBsdf(m, wo, wi, false, true);
            if (e.pdf > 0) a[std::size_t(k)] = pt::luminance(e.fcos) / e.pdf;
            if (en.pdf > 0) ac[std::size_t(k)] = pt::luminance(en.fcos) / en.pdf;
        }
        for (int k = 0; k < kRef; ++k) {
            const float u1 = r.next(), u2 = r.next(), rr = std::sqrt(u1), ph = 6.2831853f * u2;
            const Vec3 wi{rr * std::cos(ph), rr * std::sin(ph), std::sqrt(std::max(0.0f, 1.0f - u1))};
            const pt::Eval e = pt::evalBsdf(m, wo, wi);
            b[std::size_t(k)] = wi.z > 0 ? pt::luminance(e.fcos) / (wi.z / pt::kPi) : 0.0;
        }
        const Est A = meanSe(a), C = meanSe(ac), B = meanSe(b), N = meanSe(ndfMixture(m, wo, r, kRef));
        const auto q = [](Est x, Est y) { return std::fabs(x.mean - y.mean) / (4.0 * std::sqrt(x.se * x.se + y.se * y.se) + 1e-3); };
        ratio[std::size_t(c)] = q(A, N); ctlRatio[std::size_t(c)] = q(C, N); cosRatio[std::size_t(c)] = q(A, B);
        if (std::getenv("RAW_NATIVE_P0_DUMP") && (ratio[std::size_t(c)] > 0.5 || cosRatio[std::size_t(c)] > 1.0))   // diagnosis
            std::printf("  case %d rough %.4f metal %.3f spec %.3f mu %.4f: tracer %.6f (se %.2e) ndf-mixture %.6f (se %.2e) cosine %.6f (se %.2e)\n",
                        c, m.rough, m.metallic, m.specular, mu, A.mean, A.se, N.mean, N.se, B.mean, B.se);
    });
    double worst = 0, ctl = 0, cosWorst = 0;
    int outside = 0, ctlOutside = 0, cosOutside = 0;
    for (int c = 0; c < kCases; ++c) {
        worst = std::max(worst, ratio[std::size_t(c)]); ctl = std::max(ctl, ctlRatio[std::size_t(c)]); cosWorst = std::max(cosWorst, cosRatio[std::size_t(c)]);
        outside += ratio[std::size_t(c)] > 1.0; ctlOutside += ctlRatio[std::size_t(c)] > 1.0; cosOutside += cosRatio[std::size_t(c)] > 1.0;
    }
    std::printf("P0 bsdf consistency: %d cases against the NDF mixture, worst ratio to bound %.3f, outside %d | control (NDF pdf) worst %.3f, outside %d"
                " | reported: against cosine sampling worst %.3f, outside %d\n", kCases, worst, outside, ctl, ctlOutside, cosWorst, cosOutside);
    CHECK(outside == 0);
    CHECK(ctlOutside > 0);
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"P0\", \"cases\": %d, \"reference\": \"ndf-mixture\", \"worst_ratio_to_bound\": %.4f, \"outside\": %d, "
                  "\"control_ndf_pdf_worst_ratio\": %.4f, \"control_outside\": %d, \"reported_cosine_reference_worst\": %.4f, \"reported_cosine_outside\": %d}",
                  kCases, worst, outside, ctl, ctlOutside, cosWorst, cosOutside);
    add(b);
}
// The furnace on the CPU: mean within 4 standard errors of E / (1 - a); every pixel within 6 of its own.
void furnace(float a, bool dropCos) {
    const float E = 1.0f;
    const PtScene s = ptFurnace(E, a);
    PathTraceDesc d;
    d.spp = 64; d.maxBounces = 64; d.russianRoulette = false; d.width = 48; d.height = 32;
    d.camera = d.previous = cameraOf(furnaceBox());
    d.controls.dropLambertCosine = dropCos;
    const PathTraceOutput o = pathTraceCpu(s, d);
    const double expect = E / (1.0 - a);
    double sum = 0, var = 0, worstPx = 0;
    const std::size_t n = o.depth.size();
    for (std::size_t p = 0; p < n; ++p) {
        const double l = pt::luminance({o.radiance[p * 3], o.radiance[p * 3 + 1], o.radiance[p * 3 + 2]});
        sum += l; var += o.variance[p];
        worstPx = std::max(worstPx, std::fabs(l - expect) / std::sqrt(std::max(1e-20, double(o.variance[p]))));
    }
    const double mean = sum / double(n), se = std::sqrt(var) / double(n), z = std::fabs(mean - expect) / se;
    const bool ok = z <= 4.0 && worstPx <= 6.0;
    std::printf("P1 cpu furnace a=%.1f%s: mean %.5f expect %.5f, %.2f standard errors; worst pixel %.2f of its own -> %s\n",
                a, dropCos ? " (control: no cosine)" : "", mean, expect, z, worstPx, ok ? "within" : "outside");
    CHECK(dropCos ? !ok : ok);
    char b[300];
    std::snprintf(b, sizeof b, "{\"check\": \"P1 cpu\", \"albedo\": %.1f, \"control_drop_cosine\": %s, \"mean\": %.6f, \"expected\": %.6f, \"z\": %.3f, \"worst_pixel_z\": %.3f, \"within\": %s}",
                  a, dropCos ? "true" : "false", mean, expect, z, worstPx, ok ? "true" : "false");
    add(b);
}
void determinism() {
    const PtScene s = ptRetroRoom();
    PathTraceDesc d;
    d.spp = 4; d.width = 64; d.height = 48;
    d.camera = d.previous = cameraOf(swr::retroRoom());
    const PathTraceOutput a = pathTraceCpu(s, d, 1), b = pathTraceCpu(s, d, hw());
    const bool same = std::memcmp(a.radiance.data(), b.radiance.data(), a.radiance.size() * 4) == 0 &&
                      std::memcmp(a.variance.data(), b.variance.data(), a.variance.size() * 4) == 0;
    std::printf("P3 cpu: 1 thread against %d threads: %s\n", hw(), same ? "byte-identical" : "DIFFER");
    CHECK(same);
    add(std::string("{\"check\": \"P3 cpu threads\", \"identical\": ") + (same ? "true" : "false") + "}");
}
}  // namespace

int main(int argc, char** argv) {
    p0();
    furnace(0.5f, false);
    furnace(0.8f, false);
    furnace(0.5f, true);
    determinism();
    if (argc > 1)
        std::ofstream(argv[1]) << "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R2, the path tracer's CPU checks (P0, P1 on the reference, P3 threads)\",\n"
                               << " \"pass\": " << (raw_test_failures() == 0 ? "true" : "false") << ",\n \"results\": [\n" << js << "\n ]\n}\n";
    return raw_test_summary();
}
