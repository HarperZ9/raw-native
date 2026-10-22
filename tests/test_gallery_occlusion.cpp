// The gallery's visibility pass (evidence/gallery-occlusion-bounds.json o2, o3): the pass's direction
// counts against dense estimates on 2,000 samples; and a sanity pair: a lone sphere is unoccluded,
// and a sample facing a neighbour across the gallery's gap is not.
//   test_gallery_occlusion
#include "raw/renderer/gallery.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace raw;

int main() {
    const gallery::Gallery g = gallery::build(96, 96, 1);
    double dSum = 0, dWorst = 0, sSum = 0, sWorst = 0; int n = 0;
    const std::size_t stride = std::max<std::size_t>(1, g.samples.size() / 2000);
    for (std::size_t k = 0; k < g.samples.size() && n < 2000; k += stride, ++n) {
        const lighting::Sample& s = g.samples[k];
        const double d = std::fabs(gallery::diffuseVisibility(g, s, 256) - gallery::diffuseVisibility(g, s, 16384));
        const double p = std::fabs(gallery::specularVisibility(g, s, s.m.roughness, 64) - gallery::specularVisibility(g, s, s.m.roughness, 4096));
        dSum += d; dWorst = std::max(dWorst, d); sSum += p; sWorst = std::max(sWorst, p);
    }
    std::printf("o2 diffuse visibility, 256 against 16,384 directions over %d samples: mean %.4f worst %.4f (bounds 0.01, 0.05)\n", n, dSum / n, dWorst);
    std::printf("o3 specular visibility, 64 against 4,096 directions: mean %.4f worst %.4f (bounds 0.03, 0.15)\n", sSum / n, sWorst);
    CHECK(dSum / n <= 0.01 && dWorst <= 0.05);
    CHECK(sSum / n <= 0.03 && sWorst <= 0.15);
    // A lone sphere sees everything; a point on a sphere's side facing its neighbour does not.
    gallery::Gallery one = g; one.spheres.resize(1);
    lighting::Sample top; top.p = {one.spheres[0].centre.x, one.spheres[0].centre.y + gallery::kRadius, one.spheres[0].centre.z}; top.n = {0, 1, 0}; top.t = {1, 0, 0}; top.v = {0, 0, 1};
    CHECK(gallery::diffuseVisibility(one, top, 256) == 1.0);
    lighting::Sample side; const lighting::D3 c = gallery::sphereCentre(3, 3);
    side.p = {c.x + gallery::kRadius, c.y, c.z}; side.n = {1, 0, 0}; side.t = {0, 0, -1}; side.v = {0, 0, 1};
    const double sv = gallery::diffuseVisibility(g, side, 256);
    std::printf("sanity: a side point facing its neighbour 0.06 away sees %.3f of its hemisphere\n", sv);
    CHECK(sv < 0.6);
    return raw_test_summary();
}
