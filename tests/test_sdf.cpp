// RT stage R3, M1 (evidence/rt-r3-bounds.json): float64 sphere tracing of a sphere, a box and a
// torus against independent answers: closed forms for the sphere and the box (slabs in the
// box's frame); for the torus, a root of its exact distance function bracketed at 1e-3 steps
// and bisected to 1e-12. The closest approach of each ray, for the grazing exemption, comes
// from the same closed forms or dense samples of the exact distance.
//   test_sdf [out.json]
#include "raw/renderer/sdf.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
using namespace raw;
using namespace raw::sdf;

namespace {
std::string js;
struct Truth { bool hit; double t, closest; };
D3 at(D3 o, D3 d, double t) { return {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t}; }
double dotd(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Closest approach and first root of an exact distance function along the ray, by dense sampling.
template<class F> Truth bracket(F f, D3 o, D3 d, double tMax) {
    Truth r{false, 0.0, 1e30};
    double prev = f(at(o, d, 0.0));
    for (double t = 1e-3; t < tMax; t += 1e-3) {
        const double v = f(at(o, d, t));
        r.closest = std::min(r.closest, std::fabs(v));
        if (prev > 0.0 && v <= 0.0) {
            double a = t - 1e-3, b = t;
            for (int i = 0; i < 60; ++i) { const double m = 0.5 * (a + b); (f(at(o, d, m)) > 0.0 ? a : b) = m; }
            r.hit = true; r.t = 0.5 * (a + b);
            return r;
        }
        prev = v;
    }
    return r;
}
struct Counts { long rays{0}, hitsAgree{0}, missAgree{0}, disagree{0}, exempt{0}, tOver{0}; double worstT{0}; };
template<class F> Counts run(const Program& p, F exact, const Scene& cam, double eps, bool refine = true) {
    Counts c;
    const int W = 256, H = 256;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            ++c.rays;
            const D3 d = cameraRay(cam, x, y, W, H), o = d3(cam.eye);
            const MarchHit m = march(p, o, d, 20.0, eps, refine);
            const Truth tr = exact(o, d);
            if (m.hit != tr.hit) { (tr.closest <= 1e-3 ? c.exempt : c.disagree) += 1; continue; }
            if (!m.hit) { ++c.missAgree; continue; }
            ++c.hitsAgree;
            const double r = std::fabs(m.t - tr.t) / (2e-4 * (1.0 + tr.t));
            c.worstT = std::max(c.worstT, r);
            c.tOver += r > 1.0;
        }
    return c;
}
void report(const char* name, const Counts& a, const Counts& ctl) {
    std::printf("M1 %-6s %ld rays: hits %ld, misses %ld, disagree %ld (grazing exempt %ld), t over bound %ld, worst t ratio %.3f | control (eps 1e-2, no refinement) worst %.1f\n",
                name, a.rays, a.hitsAgree, a.missAgree, a.disagree, a.exempt, a.tOver, a.worstT, ctl.worstT);
    CHECK(a.hitsAgree > 5000);
    CHECK(a.disagree == 0 && a.tOver == 0);
    CHECK(ctl.worstT > 1.0);
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"M1\", \"primitive\": \"%s\", \"rays\": %ld, \"hits\": %ld, \"misses\": %ld, \"disagree\": %ld, \"grazing_exempt\": %ld, "
                  "\"t_over_bound\": %ld, \"worst_t_ratio\": %.4f, \"control_eps_1e-2_unrefined_worst_t_ratio\": %.2f}",
                  name, a.rays, a.hitsAgree, a.missAgree, a.disagree, a.exempt, a.tOver, a.worstT, ctl.worstT);
    js += (js.empty() ? "  " : ",\n  ") + std::string(b);
}
}  // namespace

int main(int argc, char** argv) {
    Scene cam;
    cam.eye = {0.3f, 0.4f, 0.0f}; cam.target = {0.0f, 0.0f, -4.0f}; cam.fovy = 0.7f;
    {   // sphere r = 1 at (0, 0, -4)
        Program p; p.push({0, 0, -4}); p.sphere(1.0f, 0); p.pop();
        const auto exact = [](D3 o, D3 d) {
            const D3 oc{o.x, o.y, o.z + 4.0};
            const double b = dotd(oc, d), c = dotd(oc, oc) - 1.0, disc = b * b - c;
            const double perp = std::sqrt(std::max(0.0, dotd(oc, oc) - b * b));
            return disc < 0 ? Truth{false, 0, std::fabs(perp - 1.0)} : Truth{true, -b - std::sqrt(disc), std::fabs(perp - 1.0)};
        };
        report("sphere", run(p, exact, cam, 1e-4), run(p, exact, cam, 1e-2, false));
    }
    {   // box half (0.8, 0.6, 0.7) at (0, 0, -4), rotated about y by 0.5
        const float c = std::cos(0.5f), s = std::sin(0.5f), r[9] = {c, 0, -s, 0, 1, 0, s, 0, c};
        Program p; p.push({0, 0, -4}, r, 1.0f); p.box({0.8f, 0.6f, 0.7f}, 0); p.pop();
        const auto local = [r](D3 v) { return D3{r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z, r[6] * v.x + r[7] * v.y + r[8] * v.z}; };
        const auto exactSdf = [&](D3 q) { return eval(p, q).d; };
        const auto exact = [&](D3 o, D3 d) {
            const D3 lo = local({o.x, o.y, o.z + 4.0}), ld = local(d);
            const double h[3] = {0.8, 0.6, 0.7}, oo[3] = {lo.x, lo.y, lo.z}, dd[3] = {ld.x, ld.y, ld.z};
            double t0 = 0.0, t1 = 1e30;
            for (int k = 0; k < 3; ++k) {
                const double a = (-h[k] - oo[k]) / dd[k], b = (h[k] - oo[k]) / dd[k];
                t0 = std::max(t0, std::min(a, b)); t1 = std::min(t1, std::max(a, b));
            }
            Truth tr = bracket(exactSdf, o, d, 20.0);   // closest approach from dense samples
            tr.hit = t0 <= t1; tr.t = t0;
            return tr;
        };
        report("box", run(p, exact, cam, 1e-4), run(p, exact, cam, 1e-2, false));
    }
    {   // torus R = 1, r = 0.3 at (0, 0, -4), tilted about x by 0.8
        const float c = std::cos(0.8f), s = std::sin(0.8f), r[9] = {1, 0, 0, 0, c, s, 0, -s, c};
        Program p; p.push({0, 0, -4}, r, 1.0f); p.torus(1.0f, 0.3f, 0); p.pop();
        const auto exactSdf = [&](D3 q) { return eval(p, q).d; };
        const auto exact = [&](D3 o, D3 d) { return bracket(exactSdf, o, d, 20.0); };
        report("torus", run(p, exact, cam, 1e-4), run(p, exact, cam, 1e-2, false));
    }
    if (argc > 1)
        std::ofstream(argv[1]) << "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R3, M1: sphere tracing against analytic intersections\",\n"
                               << " \"pass\": " << (raw_test_failures() == 0 ? "true" : "false") << ",\n \"results\": [\n" << js << "\n ]\n}\n";
    return raw_test_summary();
}
