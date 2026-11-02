// Effect C1's measured inputs and CPU reference (K3 of evidence/hw-c1-caustic-bounds.json): the
// glasses reproduce their catalogue nd and vd, illuminant A matches the CIE table, glass triangles
// face outward, the no-glass view sees no glass, and the CPU tracer is deterministic and dispersive.
#include "raw/renderer/rt_caustic.hpp"
#include "../src/renderer/rt_caustic_math.hpp"
#include "check.hpp"
#include <cmath>
using namespace raw;
using namespace raw::rt;

int main() {
    for (const Glass* g : {&glassNBK7(), &glassNSF11()}) {
        const double nd = glassIndex(*g, 587.5618), nF = glassIndex(*g, 486.1327), nC = glassIndex(*g, 656.2725);
        CHECK_NEAR(nd, g->nd, 5e-5);
        CHECK_NEAR((nd - 1.0) / (nF - nC), g->vd, 0.05);
    }
    // CIE 15:2004 table values of S_A, relative to 100 at 560 nm: 9.7951 at 380 nm and 241.6751 at 780 nm.
    CHECK_NEAR(caustic::illuminantA(560.0), 1.0, 1e-12);
    CHECK_NEAR(caustic::illuminantA(380.0), 0.097951, 2e-4);
    CHECK_NEAR(caustic::illuminantA(780.0), 2.416751, 2e-3);
    const CausticScene s = causticPrismRoom(0);
    int glassTris = 0, inward = 0;
    for (std::size_t t = 0; t < s.material.size(); ++t) {
        if (s.materials[s.material[t]].kind != CausticKind::Glass) continue;
        ++glassTris;
        const float* v = &s.tris[t * 9];
        const Vec3 a{v[0], v[1], v[2]}, b{v[3], v[4], v[5]}, c{v[6], v[7], v[8]};
        const Vec3 ng = cross(b - a, c - a), ns{s.normals[t * 9] + s.normals[t * 9 + 3] + s.normals[t * 9 + 6],
                                                s.normals[t * 9 + 1] + s.normals[t * 9 + 4] + s.normals[t * 9 + 7],
                                                s.normals[t * 9 + 2] + s.normals[t * 9 + 5] + s.normals[t * 9 + 8]};
        inward += dot(ng, ns) <= 0.0f;
    }
    CHECK(glassTris > 1000);
    CHECK(inward == 0);
    CHECK(causticGlassPixels(causticPrismRoom(1, 8.0f, 30.0f), 96, 54) == 0);
    CHECK(causticGlassPixels(s, 96, 54) > 0);
    CausticDesc d;
    d.width = 48; d.height = 27; d.spp = 4; d.photonsPerPixel = 4;
    const CausticImage a = causticCpu(s, d), b = causticCpu(s, d);
    d.controls.noDispersion = true;
    const CausticImage n = causticCpu(s, d);
    double sum = 0, diff = 0;
    for (std::size_t q = 0; q < a.xyz.size(); ++q) { sum += a.xyz[q]; diff += std::fabs(a.xyz[q] - n.xyz[q]); CHECK(std::isfinite(a.xyz[q])); }
    CHECK(sum > 0.0);
    CHECK(a.xyz == b.xyz);
    CHECK(diff > 0.0);
    return raw_test_summary();
}
