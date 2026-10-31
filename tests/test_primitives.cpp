#include "raw/math/primitives.hpp"
#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace raw;
namespace {
struct Rng {
    std::uint64_t s;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); }
};
// A closed mesh: a cube of n x n quads a face, each vertex pushed out by a random amount along
// its direction from the centre and rotated off the axes, so triangles share edges and vertices
// with the same float positions and no edge lies on an axis plane.
std::vector<Tri> closedMesh(int n, Rng& r) {
    std::vector<Vec3> grid;   // pairs: the vertex on the cube, then where it moved
    const auto vertex = [&](Vec3 p) {
        const float k = 1.0f + 0.15f * r.next();
        p = normalize(p) * (length(p) * k);
        const float c = 0.8f, s = 0.6f;                                // rotate about y, then x
        p = {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
        return Vec3{p.x, c * p.y - s * p.z, s * p.y + c * p.z};
    };
    std::vector<Tri> tris;
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int f = 0; f < 6; ++f) {
        const Vec3 nrm = axes[f / 2] * (f % 2 ? -1.0f : 1.0f), a = axes[(f / 2 + 1) % 3], b = axes[(f / 2 + 2) % 3];
        std::vector<Vec3> v(std::size_t((n + 1) * (n + 1)));
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) v[std::size_t(j * (n + 1) + i)] = nrm + a * (2.0f * float(i) / float(n) - 1.0f) + b * (2.0f * float(j) / float(n) - 1.0f);
        for (Vec3& p : v) {
            bool found = false;
            for (std::size_t g = 0; g < grid.size(); g += 2)
                if (grid[g].x == p.x && grid[g].y == p.y && grid[g].z == p.z) { p = grid[g + 1]; found = true; break; }
            if (!found) { const Vec3 q = vertex(p); grid.push_back(p); grid.push_back(q); p = q; }
        }
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const Vec3 p00 = v[std::size_t(j * (n + 1) + i)], p10 = v[std::size_t(j * (n + 1) + i + 1)];
                const Vec3 p01 = v[std::size_t((j + 1) * (n + 1) + i)], p11 = v[std::size_t((j + 1) * (n + 1) + i + 1)];
                tris.push_back({p00, p10, p11});
                tris.push_back({p00, p11, p01});
            }
    }
    return tris;
}
// Rays from inside the closed mesh aimed at its vertices, at its edges' midpoints and at random
// points on its edges: each must hit at least one triangle. Returns the rays that leak.
int leaks(const std::vector<Tri>& tris, Rng& r, int& rays) {
    int out = 0;
    rays = 0;
    const auto probe = [&](Vec3 o, Vec3 target) {
        const Ray ray{o, normalize(target - o)};
        ++rays;
        float t, u, v;
        for (const Tri& tri : tris) if (intersectTri(ray, tri, t, u, v)) return;
        ++out;
    };
    for (const Tri& tri : tris) {
        const Vec3 o{0.3f * (r.next() - 0.5f), 0.3f * (r.next() - 0.5f), 0.3f * (r.next() - 0.5f)};
        const Vec3 e[3][2] = {{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}};
        probe(o, tri.a);
        for (const auto& ed : e) {
            probe(o, (ed[0] + ed[1]) * 0.5f);
            for (int k = 0; k < 8; ++k) { const float s = r.next(); probe(o, ed[0] + (ed[1] - ed[0]) * s); }
        }
    }
    return out;
}
}  // namespace

int main() {
    Tri tri{{-1,-1,0},{1,-1,0},{0,1,0}};
    float t,u,v;
    // ray from +z toward -z hits the triangle at t=5
    CHECK(intersectTri(Ray{{0,0,5},{0,0,-1}}, tri, t,u,v));
    CHECK_NEAR(t, 5.0, 1e-4);
    // ray that misses
    CHECK(!intersectTri(Ray{{5,5,5},{0,0,-1}}, tri, t,u,v));
    // barycentrics: u weights b, v weights c (the order every caller reads)
    CHECK(intersectTri(Ray{{0.5f,-0.5f,5},{0,0,-1}}, tri, t,u,v));
    CHECK_NEAR(u, 0.625, 1e-5); CHECK_NEAR(v, 0.25, 1e-5);
    // hits from behind count (no culling); t <= 1e-4 does not
    CHECK(intersectTri(Ray{{0,0,-2},{0,0,1}}, tri, t,u,v));
    CHECK(!intersectTri(Ray{{0,0,5e-5f},{0,0,-1}}, tri, t,u,v));
    // Watertight (Woop, Benthin and Wald, JCGT 2013): no ray through a shared edge or vertex of a
    // closed mesh slips between its triangles.
    Rng r{0x5eedULL};
    int rays = 0, leaked = 0;
    for (int n : {1, 3, 7}) {
        const std::vector<Tri> mesh = closedMesh(n, r);
        int k = 0;
        leaked += leaks(mesh, r, k);
        rays += k;
    }
    std::printf("watertight: %d of %d rays through shared edges and vertices leaked\n", leaked, rays);
    CHECK(leaked == 0);
    AABB box{{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
    box.grow({1,2,3}); box.grow({-1,0,5});
    CHECK_NEAR(box.mn.x,-1,1e-6); CHECK_NEAR(box.mx.z,5,1e-6);
    return raw_test_summary();
}
