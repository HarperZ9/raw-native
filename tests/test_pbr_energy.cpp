// Energy of the glTF material model (evidence/m3-materials-bounds.json): white furnaces for
// metal, dielectric and anisotropic metal, conservation in every layered family, and the
// rough interface's reflection plus transmission at low roughness.
//   test_pbr_energy [--json]
#include "pbr_quadrature.hpp"
#include "raw/core/parallel.hpp"
#include "check.hpp"
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using namespace pbr_test;

namespace {
struct Point { std::string family; Material m; double mu, azimuth; Rgb a; };
struct Family { const char* name; double lo, hi; int count{0}; double worst{0}; int outside{0}; };   // pass when lo <= albedo <= hi

Material white() { Material m; m.baseColor = {1, 1, 1}; return m; }
void grid(std::vector<Point>& p) {
    for (int kr = 1; kr <= 20; ++kr) for (int km = 1; km <= 20; ++km) {
        Material m = white(); m.roughness = kr * 0.05;
        p.push_back({"furnace_metal", m, km * 0.05, 0.0, {}});
        for (double ior : {1.33, 1.5, 2.0}) {
            Material d = m; d.metallic = 0.0; d.ior = ior;
            p.push_back({"furnace_dielectric", d, km * 0.05, 0.0, {}});
        }
    }
    for (double an : {0.3, 0.6, 1.0}) for (int kr = 1; kr <= 5; ++kr) for (int km = 1; km <= 10; ++km) for (double az : {0.0, 45.0, 90.0}) {
        Material m = white(); m.roughness = kr * 0.2; m.anisotropy = an;
        p.push_back({"furnace_anisotropic", m, km * 0.1, az * kPi / 180.0, {}});
    }
    // Added 2026-10-10 after the GPU parity dump showed a negative sheen weighting at mu 0.06:
    // the grazing views 0.02 and 0.05 join every conservation family (a tightening).
    const double mus[] = {0.02, 0.05, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0};
    for (int base = 0; base < 2; ++base) {
        Material b = white(); b.roughness = 0.5; b.metallic = base == 0 ? 1.0 : 0.0;
        for (double mu : mus) {
            for (double c : {0.5, 1.0}) for (double rc : {0.05, 0.25, 0.5, 0.75, 1.0}) {
                Material m = b; m.clearcoat = c; m.clearcoatRoughness = rc;
                p.push_back({"conservation", m, mu, 0.0, {}});
            }
            for (double rs : {0.1, 0.3, 0.5, 0.7, 1.0}) {
                Material m = b; m.sheenColor = {1, 1, 1}; m.sheenRoughness = rs;
                p.push_back({"conservation", m, mu, 0.0, {}});
            }
            for (double th : {100.0, 400.0, 700.0, 1000.0}) {
                Material m = b; m.iridescence = 1.0; m.iridescenceThickness = th;
                p.push_back({base == 0 ? "conservation" : "conservation_iridescent_dielectric", m, mu, 0.0, {}});
            }
        }
    }
    for (double mu : mus) for (double r : {0.1, 0.5, 1.0}) {
        for (double tr : {0.5, 1.0}) {
            Material m = white(); m.metallic = 0.0; m.roughness = r; m.transmission = tr;
            p.push_back({"conservation", m, mu, 0.0, {}});
        }
        Material v = white(); v.metallic = 0.0; v.roughness = r; v.transmission = 1.0; v.volume = true; v.ior = 1.5;
        p.push_back({"conservation", v, mu, 0.0, {}});
    }
    for (double mu : {0.2, 0.4, 0.6, 0.8, 1.0}) {
        Material v = white(); v.metallic = 0.0; v.roughness = 0.05; v.transmission = 1.0; v.volume = true; v.ior = 1.5;
        p.push_back({"interface_total", v, mu, 0.0, {}});
    }
}
}  // namespace

int main(int argc, char** argv) {
    const bool json = argc > 1 && std::strcmp(argv[1], "--json") == 0;
    const Tables t;
    std::vector<Point> pts;
    grid(pts);
    warm();
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    raw::parallelRows(int(pts.size()), threads, [&](int k) { pts[k].a = albedo(pts[k].m, t, view(pts[k].mu, pts[k].azimuth)); });
    Family fam[] = {{"furnace_metal", 1 - 1e-3, 1 + 1e-3}, {"furnace_dielectric", 1 - 1e-3, 1 + 1e-3},
                    {"furnace_anisotropic", 1 - 2e-2, 1 + 2e-2}, {"conservation", -1e9, 1 + 1e-3},
                    {"conservation_iridescent_dielectric", -1e9, 1 + 2e-2}, {"interface_total", 0.99, 1 + 1e-3}};
    if (json) std::printf("{\n \"points\": [\n");
    for (std::size_t k = 0; k < pts.size(); ++k) {
        const Point& p = pts[k];
        for (Family& f : fam) {
            if (p.family != f.name) continue;
            ++f.count;
            for (double v : {p.a.r, p.a.g, p.a.b}) {
                const double dev = f.lo < 0 ? v - 1.0 : std::fabs(v - 1.0);
                f.worst = std::max(f.worst, dev);
                if (v < f.lo || v > f.hi) ++f.outside;
            }
        }
        if (json) std::printf("  {\"family\": \"%s\", \"roughness\": %.2f, \"mu\": %.2f, \"azimuth_deg\": %.0f, \"albedo\": [%.6f, %.6f, %.6f]}%s\n",
                              p.family.c_str(), p.m.roughness, p.mu, p.azimuth * 180.0 / kPi, p.a.r, p.a.g, p.a.b, k + 1 < pts.size() ? "," : "");
    }
    if (json) std::printf(" ],\n \"families\": [\n");
    for (std::size_t i = 0; i < std::size(fam); ++i) {
        const Family& f = fam[i];
        if (json) std::printf("  {\"family\": \"%s\", \"points\": %d, \"outside_bound\": %d, \"worst\": %.3e, \"bound_lo\": %.4f, \"bound_hi\": %.4f}%s\n",
                              f.name, f.count, f.outside, f.worst, f.lo, f.hi, i + 1 < std::size(fam) ? "," : "");
        else std::printf("%-36s %4d points, %d outside the bound, worst %.3e (conservation: excess over 1; furnaces: |albedo - 1|; interface: |R+T - 1|)\n", f.name, f.count, f.outside, f.worst);
        CHECK(f.count > 0);
        CHECK(f.outside == 0);
    }
    if (json) std::printf(" ],\n \"failures\": %d\n}\n", raw_test_failures());
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
