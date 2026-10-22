// The material gallery at another sphere spacing (evidence/m3-gallery-miss-diagnosis.json): the same
// spheres, materials, lights and sky, the camera's field of view widened to keep the grid in frame.
// Far-apart spheres remove the neighbours' occlusion and reflections, which the per-sample raster
// has no term for, so the comparison with the path tracer isolates the shading model.
#include "raw/renderer/gallery.hpp"
#include <cmath>
namespace raw::gallery {
namespace {
lighting::D3 unit(lighting::D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return {a.x / l, a.y / l, a.z / l}; }
}  // namespace

Gallery buildSpaced(int size, int spp, double spacing) {
    const Gallery ref = build(16, 16, 1);   // the lights, sky and exposure
    Gallery g;
    g.width = g.height = size; g.spp = spp < 1 ? 1 : spp; g.name = "material-gallery-spaced";
    g.lights = ref.lights; g.env = ref.env; g.sh = ref.sh; g.exposure = ref.exposure;
    g.fovy = 2.0 * std::atan(3.6 * spacing / kDepth);
    g.grid = ref.grid; g.grid.fovy = g.fovy; g.grid.aspect = 1.0;
    g.background.assign(std::size_t(size) * size, {}); g.hits.assign(std::size_t(size) * size, 0);
    for (int r = 0; r < kRows; ++r) {
        std::string label;
        for (int c = 0; c < kCols; ++c) g.spheres.push_back({{(c - 3) * spacing, (3 - r) * spacing, -kDepth}, kRadius, sphereMaterial(r, c, &label), r});
        g.groupLabels.push_back(label);
    }
    const double th = std::tan(0.5 * g.fovy), inv = 1.0 / (g.spp * g.spp);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) for (int sy = 0; sy < g.spp; ++sy) for (int sx = 0; sx < g.spp; ++sx) {
        const double u = (x + (sx + 0.5) / g.spp) / size * 2.0 - 1.0, v = 1.0 - (y + (sy + 0.5) / g.spp) / size * 2.0;
        const lighting::D3 dir = unit({u * th, v * th, -1.0});
        double best = 1e30; int hit = -1;
        for (std::size_t i = 0; i < g.spheres.size(); ++i) {
            const lighting::D3 o = g.spheres[i].centre;
            const double b = dir.x * o.x + dir.y * o.y + dir.z * o.z, disc = b * b - (o.x * o.x + o.y * o.y + o.z * o.z - kRadius * kRadius);
            if (disc < 0.0) continue;
            const double t = b - std::sqrt(disc);
            if (t > 0.0 && t < best) { best = t; hit = int(i); }
        }
        const int pix = y * size + x;
        if (hit < 0) {
            const Rgb L = g.env.sample(0, dir);
            Rgb& bg = g.background[std::size_t(pix)];
            bg = {bg.r + L.r * g.exposure * inv, bg.g + L.g * g.exposure * inv, bg.b + L.b * g.exposure * inv};
            continue;
        }
        const Gallery::Sphere& sp = g.spheres[std::size_t(hit)];
        lighting::Sample s;
        s.p = {dir.x * best, dir.y * best, dir.z * best};
        s.n = unit({s.p.x - sp.centre.x, s.p.y - sp.centre.y, s.p.z - sp.centre.z});
        s.t = unit({-s.n.z, 0.0, s.n.x});
        if (!std::isfinite(s.t.x)) s.t = {1, 0, 0};
        s.v = {-dir.x, -dir.y, -dir.z}; s.m = sp.m;
        g.samples.push_back(s); g.pixel.push_back(pix); ++g.hits[std::size_t(pix)]; g.group.push_back(sp.group);
    }
    return g;
}

}  // namespace raw::gallery
