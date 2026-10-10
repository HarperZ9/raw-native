#pragma once
// Private to src/renderer: the box, quad and cylinder builder of the owned scenes
// (swr_scenes.cpp, rt_scenes.cpp).
#include "raw/renderer/swr.hpp"
#include <cmath>
namespace raw::swr {
struct Builder {
    Geometry& g;
    std::uint32_t vertex(Vec3 p, Vec3 n, Vec2 uv) {
        g.pos.push_back(p); g.nrm.push_back(n); g.uv.push_back(uv);
        return std::uint32_t(g.pos.size() - 1);
    }
    // a, b, c, d counter-clockwise seen from the front; UV spans (0,0)..(us,vs).
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, float us, float vs, std::uint32_t tex) {
        const Vec3 n = normalize(cross(b - a, d - a));
        const std::uint32_t i = vertex(a, n, {0, vs}), j = vertex(b, n, {us, vs}), k = vertex(c, n, {us, 0}), l = vertex(d, n, {0, 0});
        for (std::uint32_t v : {i, j, k, i, k, l}) g.idx.push_back(v);
        g.triTexture.push_back(tex); g.triTexture.push_back(tex);
    }
    // An axis-aligned box; texture repeats every `rep` world units.
    void box(Vec3 lo, Vec3 hi, std::uint32_t tex, float rep = 1.0f) {
        const float sx = (hi.x - lo.x) / rep, sy = (hi.y - lo.y) / rep, sz = (hi.z - lo.z) / rep;
        quad({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, sx, sy, tex);   // +z
        quad({hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, sx, sy, tex);   // -z
        quad({hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, sz, sy, tex);   // +x
        quad({lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, sz, sy, tex);   // -x
        quad({lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, sx, sz, tex);   // +y
        quad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, sx, sz, tex);   // -y
    }
    void cylinder(Vec3 base, float r, float h, int seg, std::uint32_t tex) {
        for (int i = 0; i < seg; ++i) {
            const float a0 = 6.2831853f * float(i) / float(seg), a1 = 6.2831853f * float(i + 1) / float(seg);
            const Vec3 p0{base.x + r * std::cos(a0), base.y, base.z - r * std::sin(a0)};
            const Vec3 p1{base.x + r * std::cos(a1), base.y, base.z - r * std::sin(a1)};
            quad(p0, p1, {p1.x, p1.y + h, p1.z}, {p0.x, p0.y + h, p0.z}, 1.0f / float(seg) * 4.0f, h, tex);
        }
    }
};
}  // namespace raw::swr
