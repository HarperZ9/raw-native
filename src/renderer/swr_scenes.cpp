// The rasterizer's owned scenes: see raw/renderer/swr_scenes.hpp.
#include "raw/renderer/swr_scenes.hpp"
#include <cmath>
namespace raw::swr {
namespace {
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
Scene base(const char* name) {
    Scene s;
    s.name = name;
    s.tex = proceduralTextures();
    return s;
}
}  // namespace

Scene retroRoom() {
    Scene s = base("retro_room");
    Builder b{s.geo};
    for (int z = -16; z < 2; ++z)                                  // floor tiles, running under the camera
        for (int x = -4; x < 4; ++x)
            b.quad({float(x), 0, float(z + 1)}, {float(x + 1), 0, float(z + 1)}, {float(x + 1), 0, float(z)}, {float(x), 0, float(z)}, 1, 1, kTiles);
    for (int z = -16; z < 2; z += 2) {                             // walls in 2 m panels, ceiling
        b.quad({-4, 0, float(z)}, {-4, 0, float(z + 2)}, {-4, 3.5f, float(z + 2)}, {-4, 3.5f, float(z)}, 2, 3.5f, kBrick);
        b.quad({4, 0, float(z + 2)}, {4, 0, float(z)}, {4, 3.5f, float(z)}, {4, 3.5f, float(z + 2)}, 2, 3.5f, kBrick);
        b.quad({-4, 3.5f, float(z + 2)}, {4, 3.5f, float(z + 2)}, {4, 3.5f, float(z)}, {-4, 3.5f, float(z)}, 4, 1, kPlanks);
    }
    b.quad({4, 0, -16}, {-4, 0, -16}, {-4, 3.5f, -16}, {4, 3.5f, -16}, 4, 1.75f, kPlaster);
    for (float z : {-3.0f, -7.0f, -11.0f})
        for (float x : {-2.5f, 2.5f}) b.box({x - 0.25f, 0, z - 0.25f}, {x + 0.25f, 3.5f, z + 0.25f}, kStone);
    for (int i = 0; i < 6; ++i)                                    // stairs up to the back wall
        b.box({-1.5f, 0, -16.0f + 0.4f * float(5 - i)}, {1.5f, 0.25f * float(i + 1), -16.0f + 0.4f * float(6 - i)}, kPlanks, 0.5f);
    b.box({-2.2f, 0.75f, -5.6f}, {-0.8f, 0.85f, -4.6f}, kPlanks, 0.5f);   // a table and its legs
    for (float x : {-2.1f, -0.95f}) for (float z : {-5.5f, -4.75f}) b.box({x, 0, z}, {x + 0.08f, 0.75f, z + 0.08f}, kPlanks, 0.5f);
    b.cylinder({-1.5f, 0.85f, -5.1f}, 0.08f, 0.35f, 8, kBrass);           // a lamp on it
    const float fx = 0.8f, fz = -5.0f;                                     // a low-poly figure
    b.box({fx - 0.22f, 0, fz - 0.1f}, {fx - 0.04f, 0.85f, fz + 0.1f}, kCloth, 0.5f);
    b.box({fx + 0.04f, 0, fz - 0.1f}, {fx + 0.22f, 0.85f, fz + 0.1f}, kCloth, 0.5f);
    b.box({fx - 0.28f, 0.85f, fz - 0.14f}, {fx + 0.28f, 1.5f, fz + 0.14f}, kCloth, 0.5f);
    b.box({fx - 0.42f, 0.9f, fz - 0.08f}, {fx - 0.3f, 1.45f, fz + 0.08f}, kSkin, 0.5f);
    b.box({fx + 0.3f, 0.9f, fz - 0.08f}, {fx + 0.42f, 1.45f, fz + 0.08f}, kSkin, 0.5f);
    b.box({fx - 0.13f, 1.52f, fz - 0.13f}, {fx + 0.13f, 1.8f, fz + 0.13f}, kSkin, 0.5f);
    s.eye = {0, 1.6f, 1.0f}; s.target = {0.3f, 1.2f, -8.0f}; s.fovy = 1.1f; s.nearZ = 0.1f; s.farZ = 100.0f;
    return s;
}

Scene isoStreet() {
    Scene s = base("iso_street");
    Builder b{s.geo};
    for (int z = -5; z < 5; ++z)                                   // stone blocks at varying heights
        for (int x = -5; x < 5; ++x) {
            const float h = 0.05f * float((x * 7 + z * 13 + 40) % 5);
            b.box({float(x), -0.5f, float(z)}, {float(x + 1), h, float(z + 1)}, kStone);
        }
    const float fh[5] = {3.2f, 2.4f, 4.0f, 2.8f, 3.6f};
    for (int i = 0; i < 5; ++i) {                                   // facades along the back and one side
        b.box({-5.0f + 2.0f * float(i), 0, -5.0f}, {-3.1f + 2.0f * float(i), fh[i], -3.8f}, i % 2 ? kBrick : kPlaster);
        b.box({-5.1f + 2.0f * float(i), fh[i], -5.1f}, {-3.0f + 2.0f * float(i), fh[i] + 0.15f, -3.7f}, kPlanks, 0.5f);
    }
    for (int i = 0; i < 3; ++i) b.box({-5.0f, 0, -3.6f + 2.4f * float(i)}, {-3.8f, 2.2f + 0.6f * float(i), -1.4f + 2.4f * float(i)}, i % 2 ? kPlaster : kBrick);
    for (float z : {-2.0f, 1.5f}) {                                 // lamp posts
        b.cylinder({1.5f, 0.2f, z}, 0.06f, 2.6f, 8, kBrass);
        b.box({1.35f, 2.8f, z - 0.15f}, {1.65f, 3.1f, z + 0.15f}, kBrass, 0.5f);
    }
    b.box({2.6f, 0.2f, 2.4f}, {3.2f, 0.8f, 3.0f}, kPlanks, 0.6f);   // crates
    b.box({3.0f, 0.2f, 3.1f}, {3.5f, 0.7f, 3.6f}, kPlanks, 0.5f);
    s.eye = {24, 20, 24}; s.target = {0, 0.5f, 0}; s.fovy = 0.32f; s.nearZ = 1.0f; s.farZ = 120.0f;
    s.light = {-0.5f, 0.8f, 0.3f};
    return s;
}

Scene clipStress() {
    Scene s = base("clip_stress");
    Builder b{s.geo};
    b.quad({-300, 0, 300}, {300, 0, 300}, {300, 0, -300}, {-300, 0, -300}, 300, 300, kChecker);
    // A wall along the left of the view from behind the camera to the horizon: it crosses the near
    // plane and, near the camera, the guard band's left and top planes.
    b.quad({-0.8f, 0, 1}, {-0.8f, 0, -300}, {-0.8f, 3, -300}, {-0.8f, 3, 1}, 150, 1.5f, kBrick);
    // A block beside the camera, from in front of it to behind it, so its near end is cut by the
    // near plane inside the view (the first version filled the whole view, evidence/rt-r1-runs.json run 4).
    b.box({0.12f, 0, -1.6f}, {0.5f, 0.4f, 0.5f}, kStone);
    s.eye = {0, 0.25f, 0}; s.target = {0.2f, 0.12f, -3.0f}; s.fovy = 1.2f; s.nearZ = 0.05f; s.farZ = 500.0f;
    return s;
}

Scene floorQuad() {
    Scene s = base("floor_quad");
    Builder b{s.geo};
    b.quad({-1, 0, 0}, {1, 0, 0}, {1, 0, -10}, {-1, 0, -10}, 1, 1, kChecker);
    s.eye = {0, 0.4f, 0.5f}; s.target = {0, 0, -6}; s.fovy = 0.9f; s.nearZ = 0.1f; s.farZ = 50.0f;
    return s;
}

std::vector<Scene> ownedScenes() { return {retroRoom(), isoStreet(), clipStress()}; }

ScreenMesh tieGrid(bool jitter) {
    ScreenMesh m;
    m.name = jitter ? "tie_grid_jittered" : "tie_grid";
    constexpr int N = 24;
    const float x0 = 16.5f, y0 = 16.5f, step = 9.0f;
    std::vector<Vec2> v((N + 1) * (N + 1));
    std::uint32_t r = 12345u;
    const auto rnd = [&r] { r = r * 1664525u + 1013904223u; return float(r >> 8) / 16777216.0f; };
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i) {
            Vec2 p{x0 + step * float(i), y0 + step * float(j)};
            if (jitter && i > 0 && j > 0 && i < N && j < N) { p.x += (rnd() - 0.5f) * 4.0f; p.y += (rnd() - 0.5f) * 4.0f; }
            v[std::size_t(j * (N + 1) + i)] = p;
        }
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const Vec2 a = v[std::size_t(j * (N + 1) + i)], b = v[std::size_t(j * (N + 1) + i + 1)];
            const Vec2 c = v[std::size_t((j + 1) * (N + 1) + i + 1)], d = v[std::size_t((j + 1) * (N + 1) + i)];
            for (Vec2 p : {a, b, c, a, c, d}) m.corners.push_back(p);
        }
    m.outline = {v[0], v[N], v[std::size_t((N + 1) * (N + 1) - 1)], v[std::size_t(N * (N + 1))]};
    return m;
}

ScreenMesh tieFan() {
    ScreenMesh m;
    m.name = "tie_fan";
    const Vec2 c{128.5f, 128.5f};
    constexpr int K = 64;
    std::vector<Vec2> rim(K);
    std::uint32_t r = 777u;
    for (int k = 0; k < K; ++k) {
        r = r * 1664525u + 1013904223u;
        const float a = 6.2831853f * float(k) / float(K), rad = 70.0f + float(r >> 24) / 8.0f;
        rim[std::size_t(k)] = {c.x + rad * std::cos(a), c.y + rad * std::sin(a)};
    }
    for (int k = 0; k < K; ++k) { m.corners.push_back(c); m.corners.push_back(rim[std::size_t(k)]); m.corners.push_back(rim[std::size_t((k + 1) % K)]); }
    m.outline = rim;
    return m;
}

Geometry screenGeometry(const ScreenMesh& m, float w, float h) {
    Geometry g;
    for (std::size_t i = 0; i < m.corners.size(); ++i) {
        g.pos.push_back({m.corners[i].x / w * 2.0f - 1.0f, 1.0f - m.corners[i].y / h * 2.0f, 0.0f});
        g.nrm.push_back({0, 0, 1});
        g.uv.push_back({m.corners[i].x / 32.0f, m.corners[i].y / 32.0f});
        g.idx.push_back(std::uint32_t(i));
        if (i % 3 == 0) g.triTexture.push_back(kChecker);
    }
    return g;
}

}  // namespace raw::swr
