// The white furnace (ROADMAP M2 criterion 5, evidence/m2-white-furnace-bounds.json).
// A white lobe must reflect all the light it receives: the directional albedo of
// f_ss + f_ms is 1 within 1e-3 at roughness 0.05 to 1.00 and view cosine 0.05 to 1.00.
// The single-scattering part is integrated here by Gauss-Legendre quadrature over
// the half vector, a different route from the table's VNDF sampling. With the
// multiple-scattering term removed, the furnace must fail (the control).
//   test_brdf_furnace [--json]   prints the 400-point result as JSON with --json
#include "raw/renderer/brdf.hpp"
#include "check.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>
using namespace raw::brdf;

namespace {
std::vector<double> gx, gw;   // 1-D nodes on [0, 1]

// Single scattering, F = 1: integral over the half vector h of D(h) G2 (o.h) / mu_o.
// theta_h = atan(alpha t), t = s / (1 - s), so the lobe's width is about one unit of t
// at every roughness; phi runs over [0, pi] and is doubled (o lies in the xz plane).
double albedoSingle(double muO, double a) {
    const double so = std::sqrt(1.0 - muO * muO);
    double sum = 0.0;
    for (std::size_t i = 0; i < gx.size(); ++i) {
        const double s = gx[i], t = s / (1.0 - s), dt = 1.0 / ((1.0 - s) * (1.0 - s));
        const double th = std::atan(a * t), dth = a / (1.0 + a * a * t * t) * dt;
        const double ch = std::cos(th), sh = std::sin(th), d = ggxD(ch, a);
        for (std::size_t j = 0; j < gx.size(); ++j) {
            const double phi = kPi * gx[j];
            const double oh = so * sh * std::cos(phi) + muO * ch;     // o . h
            if (oh <= 0.0) continue;
            const double muI = 2.0 * oh * ch - muO;                 // z of the reflected direction
            if (muI <= 0.0) continue;
            sum += gw[i] * gw[j] * kPi * 2.0 * d * smithG2(muO, muI, a) * oh / muO * sh * dth;
        }
    }
    return sum;
}
// Multiple scattering: 2 pi integral of f_ms(mu_o, mu_i) mu_i dmu_i.
double albedoMulti(const EnergyTable& t, double muO, double r) {
    static std::vector<double> x, w;
    if (x.empty()) gaussLegendre01(128, x, w);
    double s = 0.0;
    for (std::size_t k = 0; k < x.size(); ++k) s += w[k] * multiScatter(t, muO, x[k], r) * x[k];
    return 2.0 * kPi * s;
}
}  // namespace

int main(int argc, char** argv) {
    const bool json = argc > 1 && std::strcmp(argv[1], "--json") == 0;
    gaussLegendre01(768, gx, gw);
    const auto t0 = std::chrono::steady_clock::now();
    const EnergyTable table;
    const double buildS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double worst = 0.0, worstR = 0.0, worstMu = 0.0, controlAtOne = 0.0;
    if (json) std::printf("{\n \"table_build_s\": %.2f,\n \"points\": [\n", buildS);
    for (int kr = 1; kr <= 20; ++kr) {
        const double r = kr * 0.05, a = alphaOf(r);
        for (int km = 1; km <= 20; ++km) {
            const double mu = km * 0.05, ss = albedoSingle(mu, a), ms = albedoMulti(table, mu, r), tot = ss + ms;
            if (std::fabs(tot - 1.0) > worst) { worst = std::fabs(tot - 1.0); worstR = r; worstMu = mu; }
            if (kr == 20) controlAtOne = std::max(controlAtOne, std::fabs(ss - 1.0));
            if (json) std::printf("  {\"roughness\": %.2f, \"mu\": %.2f, \"single\": %.6f, \"multi\": %.6f, \"total\": %.6f}%s\n",
                                  r, mu, ss, ms, tot, kr == 20 && km == 20 ? "" : ",");
            CHECK_NEAR(tot, 1.0, 1e-3);
        }
    }
    if (json) std::printf(" ],\n \"max_abs_error\": %.3e,\n \"at\": {\"roughness\": %.2f, \"mu\": %.2f},\n"
                          " \"control_single_only_max_loss_at_roughness_1\": %.4f\n}\n", worst, worstR, worstMu, controlAtOne);
    else std::printf("white furnace: max |albedo - 1| = %.3e at roughness %.2f, mu %.2f; single only at roughness 1 loses up to %.4f; table %.2f s\n",
                     worst, worstR, worstMu, controlAtOne, buildS);
    CHECK(controlAtOne > 0.1);   // the control: without f_ms the furnace fails
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
