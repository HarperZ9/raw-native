// Setup of the software rasterizer: see raw/renderer/swr.hpp. Every float operation here is
// written in the order swr.wgsl's swr_setup performs it, so the two agree wherever the GPU
// rounds as IEEE float32 does (check C5 measures where they do not).
#include "raw/renderer/swr.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace raw::swr {
namespace {
struct CV { float x, y, z, w, s, t; };          // a clip-space vertex and its weights in the source triangle
float dist(const CV& v, int plane, float g) {
    switch (plane) {
    case 0: return v.z + v.w;
    case 1: return v.w - v.z;
    case 2: return g * v.w - v.x;
    case 3: return g * v.w + v.x;
    case 4: return g * v.w - v.y;
    default: return g * v.w + v.y;
    }
}
CV lerp(const CV& a, const CV& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t, a.s + (b.s - a.s) * t, a.t + (b.t - a.t) * t};
}
int clip(CV* poly, int n, float g) {
    CV out[9];
    for (int plane = 0; plane < 6 && n >= 3; ++plane) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const CV& a = poly[i];
            const CV& b = poly[(i + 1) % n];
            const float da = dist(a, plane, g), db = dist(b, plane, g);
            if (da >= 0.0f) out[m++] = a;
            if ((da >= 0.0f) != (db >= 0.0f)) out[m++] = lerp(a, b, da / (da - db));
        }
        n = m;
        for (int i = 0; i < n; ++i) poly[i] = out[i];
    }
    return n < 3 ? 0 : n;
}
std::int32_t ceilDiv256(std::int32_t v) { return -((-v) >> 8); }
struct SV { std::int32_t x, y; float z, iw, s, t, fx, fy; };
// One fan triangle into slot `slot`; a degenerate or culled triangle leaves the slot empty.
void writeSlot(Setup& S, std::size_t slot, SV a, SV b, SV c, std::uint32_t source, const Options& o) {
    std::int32_t* I = &S.ints[slot * kSetupInts];
    float* F = &S.floats[slot * kSetupFloats];
    std::int64_t area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
    if (area == 0) return;
    if (area < 0) {                              // counter-clockwise on screen: clockwise in NDC (y flips)
        if (o.cullBack) return;
        std::swap(b, c);
        area = -area;
    }
    const SV v[3] = {a, b, c};
    std::int32_t mnx = v[0].x, mxx = v[0].x, mny = v[0].y, mxy = v[0].y;
    for (int k = 0; k < 3; ++k) {
        I[2 * k] = v[k].x; I[2 * k + 1] = v[k].y;
        F[k] = v[k].z; F[3 + k] = v[k].iw; F[7 + 2 * k] = v[k].s; F[8 + 2 * k] = v[k].t;
        mnx = std::min(mnx, v[k].x); mxx = std::max(mxx, v[k].x); mny = std::min(mny, v[k].y); mxy = std::max(mxy, v[k].y);
        S.screen[slot * 6 + 2 * k] = v[k].fx; S.screen[slot * 6 + 2 * k + 1] = v[k].fy;
    }
    F[6] = 1.0f / edgeToFloat(area);
    I[6] = std::int32_t(source + 1);
    I[7] = std::max(0, ceilDiv256(mnx - 128)); I[8] = std::max(0, ceilDiv256(mny - 128));
    I[9] = std::min(S.width - 1, (mxx - 128) >> 8); I[10] = std::min(S.height - 1, (mxy - 128) >> 8);
}
SV snap(const CV& c, int w, int h, const Options& o) {
    const float iw = 1.0f / c.w;
    const float nx = c.x * iw, ny = c.y * iw, nz = c.z * iw;
    const float sx = (nx * 0.5f + 0.5f) * float(w);
    const float sy = (1.0f - (ny * 0.5f + 0.5f)) * float(h);
    const float scale = std::ldexp(1.0f, kSubpixelBits - o.snapShift);
    const std::int32_t grid = std::int32_t(1) << o.snapShift;
    const float qx = std::floor(sx * scale + 0.5f), qy = std::floor(sy * scale + 0.5f);
    return {std::int32_t(qx) * grid, std::int32_t(qy) * grid, nz, iw, c.s, c.t, sx, sy};
}
void fan(Setup& S, std::size_t base, const SV* v, int n, std::uint32_t source, const Options& o) {
    for (int k = 1; k + 1 < n; ++k) writeSlot(S, base + std::size_t(k - 1), v[0], v[k], v[k + 1], source, o);
}
void allocate(Setup& S, std::size_t tris, int w, int h) {
    S.width = w; S.height = h;
    S.ints.assign(tris * kSlotsPerTri * kSetupInts, 0);
    S.floats.assign(tris * kSlotsPerTri * kSetupFloats, 0.0f);
    S.screen.assign(tris * kSlotsPerTri * 6, 0.0f);
    for (std::size_t s = 0; s < tris * kSlotsPerTri; ++s) { S.ints[s * kSetupInts + 7] = 1; S.ints[s * kSetupInts + 9] = 0; }
}
}  // namespace

std::int64_t edge(std::int32_t xi, std::int32_t yi, std::int32_t xj, std::int32_t yj, std::int32_t px, std::int32_t py) {
    return std::int64_t(xj - xi) * std::int64_t(py - yi) - std::int64_t(yj - yi) * std::int64_t(px - xi);
}
float edgeToFloat(std::int64_t e) {
    const std::int32_t hi = std::int32_t(e >> 32);
    const std::uint32_t lo = std::uint32_t(e);
    return (float(hi) * 4294967296.0f + float(lo >> 16) * 65536.0f) + float(lo & 0xffffu);
}
bool topLeft(std::int32_t dx, std::int32_t dy) { return (dy == 0 && dx > 0) || dy < 0; }
bool covers(std::int64_t e, bool tl, FillRule r) {
    switch (r) {
    case FillRule::TopLeft: return e > 0 || (e == 0 && tl);
    case FillRule::BottomRight: return e > 0 || (e == 0 && !tl);
    case FillRule::Inclusive: return e >= 0;
    default: return e > 0;
    }
}
std::uint32_t bayer4(int x, int y) {
    static constexpr std::uint32_t B[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return B[(y & 3) * 4 + (x & 3)];
}

Mat4 Scene::viewProj(int w, int h) const {
    return mul(perspective(fovy, float(w) / float(h), nearZ, farZ), lookAt(eye, target, up));
}

Setup setup(const Geometry& g, const Mat4& M, int w, int h, const Options& o) {
    Setup S;
    allocate(S, g.triangles(), w, h);
    const float* m = M.m;
    for (std::size_t t = 0; t < g.triangles(); ++t) {
        CV poly[9];
        const float st[3][2] = {{0, 0}, {1, 0}, {0, 1}};
        for (int k = 0; k < 3; ++k) {
            const Vec3 p = g.pos[g.idx[t * 3 + k]];
            poly[k] = {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                       m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11], m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15],
                       st[k][0], st[k][1]};
        }
        const int n = clip(poly, 3, o.guardBand);
        SV sv[9];
        for (int k = 0; k < n; ++k) sv[k] = snap(poly[k], w, h, o);
        fan(S, t * kSlotsPerTri, sv, n, std::uint32_t(t), o);
    }
    return S;
}

Setup setupScreen(const std::vector<Vec2>& c, int w, int h, const Options& o) {
    Setup S;
    const std::size_t tris = c.size() / 3;
    allocate(S, tris, w, h);
    for (std::size_t t = 0; t < tris; ++t) {
        SV v[3];
        const float st[3][2] = {{0, 0}, {1, 0}, {0, 1}};
        for (int k = 0; k < 3; ++k) {
            const float scale = std::ldexp(1.0f, kSubpixelBits - o.snapShift);
            const std::int32_t grid = std::int32_t(1) << o.snapShift;
            const Vec2 p = c[t * 3 + k];
            v[k] = {std::int32_t(std::floor(p.x * scale + 0.5f)) * grid, std::int32_t(std::floor(p.y * scale + 0.5f)) * grid,
                    0.5f, 1.0f, st[k][0], st[k][1], p.x, p.y};
        }
        fan(S, t * kSlotsPerTri, v, 3, std::uint32_t(t), o);
    }
    return S;
}

std::vector<std::uint32_t> params(const Options& o, int w, int h, std::uint32_t slots, int tile, const Mat4& M,
                                  std::uint32_t triangles, Vec3 light) {
    std::vector<std::uint32_t> p(36, 0u);
    const auto f = [](float v) { std::uint32_t u; std::memcpy(&u, &v, 4); return u; };
    const std::uint32_t tilesX = std::uint32_t((w + tile - 1) / tile), tilesY = std::uint32_t((h + tile - 1) / tile);
    p[0] = std::uint32_t(w); p[1] = std::uint32_t(h); p[2] = slots; p[3] = std::uint32_t(tile);
    p[4] = tilesX; p[5] = tilesY; p[6] = std::uint32_t(o.rule); p[7] = o.depthTest ? 1u : 0u;
    p[8] = o.affine ? 1u : 0u; p[9] = std::uint32_t(o.depthBits); p[10] = o.cullBack ? 1u : 0u; p[11] = std::uint32_t(o.snapShift);
    p[12] = f(std::ldexp(1.0f, kSubpixelBits - o.snapShift)); p[13] = f(o.guardBand); p[14] = triangles;
    p[15] = f(o.depthBits > 0 ? float((1u << o.depthBits) - 1u) : 0.0f);
    for (int i = 0; i < 16; ++i) p[16 + i] = f(M.m[i]);
    const float len = std::sqrt(light.x * light.x + light.y * light.y + light.z * light.z);
    p[32] = f(light.x / len); p[33] = f(light.y / len); p[34] = f(light.z / len);
    return p;
}

}  // namespace raw::swr
