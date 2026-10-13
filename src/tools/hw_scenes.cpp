// HW H1.1 scenes and rays (raw/tools/hw_scenes.hpp).
#include "raw/tools/hw_scenes.hpp"
#include <cmath>
#include <cstdint>
namespace raw {
namespace {
struct Rng {
    uint64_t s;
    uint64_t next(){ uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    float uni(){ return (float)((next() >> 40) * (1.0 / 16777216.0)); }
    float range(float a, float b){ return a + (b - a) * uni(); }
};
constexpr float kPi = 3.14159265358979f;
Vec3 sphere(Rng& r){ const float z = r.range(-1, 1), p = r.range(0, 2 * kPi), s = std::sqrt(std::fmax(0.0f, 1 - z * z)); return {s * std::cos(p), s * std::sin(p), z}; }
void quad(std::vector<Tri>& t, Vec3 a, Vec3 b, Vec3 c, Vec3 d){ t.push_back({a, b, c}); t.push_back({a, c, d}); }
// A grid of nu x nv quads spanning origin + s*u + t*v, s, t in [0, 1].
void grid(std::vector<Tri>& t, Vec3 o, Vec3 u, Vec3 v, int nu, int nv){
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i){
        auto p = [&](int a, int b){ return o + u * ((float)a / nu) + v * ((float)b / nv); };
        quad(t, p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1));
    }
}
std::vector<Tri> terrain(){
    std::vector<Tri> t;
    constexpr int N = 160;
    auto h = [](float x, float z){ return 1.5f * std::sin(0.7f * x) * std::cos(0.5f * z) + 0.3f * std::sin(2.3f * x + 1.1f * z); };
    auto p = [&](int i, int j){ const float x = -8 + 16.0f * i / N, z = -8 + 16.0f * j / N; return Vec3{x, h(x, z), z}; };
    for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) quad(t, p(i, j), p(i, j + 1), p(i + 1, j + 1), p(i + 1, j));
    return t;
}
std::vector<Tri> hall(){
    std::vector<Tri> t;
    grid(t, {-10, 0, -6}, {20, 0, 0}, {0, 0, 12}, 40, 24);           // floor
    grid(t, {-10, 0, -6}, {20, 0, 0}, {0, 6, 0}, 40, 6);             // walls
    grid(t, {-10, 0, 6}, {20, 0, 0}, {0, 6, 0}, 40, 6);
    grid(t, {-10, 0, -6}, {0, 0, 12}, {0, 6, 0}, 24, 6);
    grid(t, {10, 0, -6}, {0, 0, 12}, {0, 6, 0}, 24, 6);
    for (int cx = 0; cx < 6; ++cx) for (int cz = 0; cz < 4; ++cz){   // 24 columns, 32 sides, 4 rings, capped
        const Vec3 c{-7.5f + 3.0f * cx, 0, -4.5f + 3.0f * cz};
        constexpr int S = 32;
        for (int s = 0; s < S; ++s){
            const float a0 = 2 * kPi * s / S, a1 = 2 * kPi * (s + 1) / S;
            const Vec3 d0{0.3f * std::cos(a0), 0, 0.3f * std::sin(a0)}, d1{0.3f * std::cos(a1), 0, 0.3f * std::sin(a1)};
            for (int r = 0; r < 4; ++r){
                const Vec3 y0{0, 1.25f * r, 0}, y1{0, 1.25f * (r + 1), 0};
                quad(t, c + d0 + y0, c + d1 + y0, c + d1 + y1, c + d0 + y1);
            }
            t.push_back({c + Vec3{0, 5, 0}, c + d1 + Vec3{0, 5, 0}, c + d0 + Vec3{0, 5, 0}});
            t.push_back({c, c + d0, c + d1});
        }
    }
    for (int s = 0; s < 10; ++s){                                     // a stair of 10 boxes
        const float x0 = 6.0f, x1 = 9.5f, z0 = -5.5f + 0.4f * s, z1 = z0 + 0.4f, y1 = 0.25f * (s + 1);
        quad(t, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
        quad(t, {x0, 0, z0}, {x1, 0, z0}, {x1, y1, z0}, {x0, y1, z0});
        quad(t, {x0, 0, z1}, {x0, y1, z1}, {x1, y1, z1}, {x1, 0, z1});
        quad(t, {x0, 0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, 0, z1});
        quad(t, {x1, 0, z0}, {x1, 0, z1}, {x1, y1, z1}, {x1, y1, z0});
        quad(t, {x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0});
    }
    return t;
}
std::vector<Tri> debris(Rng& r){
    std::vector<Tri> t;
    for (int i = 0; i < 30000; ++i){
        const Vec3 a{r.range(-2, 2), r.range(-2, 2), r.range(-2, 2)};
        t.push_back({a, a + sphere(r) * r.range(0.02f, 0.2f), a + sphere(r) * r.range(0.02f, 0.2f)});
    }
    return t;
}
void addRays(HwScene& s, Rng& r, Vec3 eye, Vec3 at){
    AABB box;
    for (const Tri& t : s.tris){ box.grow(t.a); box.grow(t.b); box.grow(t.c); }
    auto push = [&](Vec3 o, Vec3 d, float tmax){ s.rays.push_back({o.x, o.y, o.z, 1e-4f, d.x, d.y, d.z, tmax}); };
    size_t b = s.rays.size();
    const Vec3 f = normalize(at - eye), rt = normalize(cross(f, {0, 1, 0})), up = cross(rt, f);
    const float k = std::tan(0.5f * kPi / 3);   // 60 degree vertical field of view
    for (int y = 0; y < 400; ++y) for (int x = 0; x < 400; ++x){
        const float sx = (2 * (x + 0.5f) / 400 - 1) * k, sy = (1 - 2 * (y + 0.5f) / 400) * k;
        push(eye, normalize(f + rt * sx + up * sy), 1e30f);
    }
    s.kinds.push_back({"camera", b, s.rays.size()}); b = s.rays.size();
    for (int i = 0; i < 100000; ++i)
        push({r.range(box.mn.x, box.mx.x), r.range(box.mn.y, box.mx.y), r.range(box.mn.z, box.mx.z)}, sphere(r), 1e30f);
    s.kinds.push_back({"random", b, s.rays.size()}); b = s.rays.size();
    for (int i = 0; i < 80000; ++i){
        const Tri& t = s.tris[(size_t)(r.next() % s.tris.size())];
        float u = r.uni(), v = r.uni();
        if (u + v > 1){ u = 1 - u; v = 1 - v; }
        Vec3 n = normalize(cross(t.b - t.a, t.c - t.a));
        if (r.next() & 1) n = n * -1.0f;
        const Vec3 p = t.a + (t.b - t.a) * u + (t.c - t.a) * v + n * 1e-3f;
        const Vec3 ax = std::fabs(n.x) > 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 tb = normalize(cross(ax, n)), bb = cross(n, tb);
        const float u1 = r.uni(), ph = 2 * kPi * r.uni(), rr = std::sqrt(u1);
        push(p, normalize(tb * (rr * std::cos(ph)) + bb * (rr * std::sin(ph)) + n * std::sqrt(std::fmax(0.0f, 1 - u1))), 2.0f);
    }
    s.kinds.push_back({"ao", b, s.rays.size()});
}
}  // namespace

std::vector<HwScene> hwScenes(uint64_t seed){
    Rng r{seed};
    std::vector<HwScene> out(3);
    out[0].name = "terrain"; out[0].tris = terrain(); addRays(out[0], r, {-9, 5, -9}, {0, 0, 0});
    out[1].name = "hall"; out[1].tris = hall(); addRays(out[1], r, {-9, 3, -5}, {5, 1.5f, 3});
    out[2].name = "debris"; out[2].tris = debris(r); addRays(out[2], r, {0, 0.5f, -6}, {0, 0, 0});
    return out;
}
std::vector<float> flatten(const std::vector<Tri>& tris){
    std::vector<float> f;
    f.reserve(9 * tris.size());
    for (const Tri& t : tris) for (Vec3 p : {t.a, t.b, t.c}){ f.push_back(p.x); f.push_back(p.y); f.push_back(p.z); }
    return f;
}
}  // namespace raw
