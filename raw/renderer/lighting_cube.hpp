#pragma once
// Cube-map addressing, the orthonormal basis, the low-discrepancy set and the prefilter
// kernel shared by the CPU reference (raw/renderer/lighting.hpp) and, line for line,
// src/renderer/gpu/shaders/light.wgsl. Faces in order +x -x +y -y +z -z, with the
// D3D / OpenGL major-axis rules; u, v in [0, 1] with v down the face.
#include "raw/renderer/lighting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace raw::lighting::cube {

// Levels stop shrinking at 8 texels a face: the sampler clamps at face edges, and a 1- or
// 2-texel face turned the roughest reflections into flat patches with hard face seams
// (seen in the first material gallery, 2026-10-10). The roughest lobes are smooth enough
// that 8 x 8 faces hold them.
inline constexpr int kMinLevelSize = 8;
inline int levelSize(int size, int level) { return std::max(std::min(size, kMinLevelSize), size >> level); }

inline int faceOf(D3 d, double& u, double& v) {
    const double ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    int f; double ma, sc, tc;
    if (ax >= ay && ax >= az) { f = d.x >= 0 ? 0 : 1; ma = ax; sc = d.x >= 0 ? -d.z : d.z; tc = -d.y; }
    else if (ay >= az)        { f = d.y >= 0 ? 2 : 3; ma = ay; sc = d.x; tc = d.y >= 0 ? d.z : -d.z; }
    else                      { f = d.z >= 0 ? 4 : 5; ma = az; sc = d.z >= 0 ? d.x : -d.x; tc = -d.y; }
    u = 0.5 * (sc / ma + 1.0); v = 0.5 * (tc / ma + 1.0);
    return f;
}
inline D3 dirOf(int f, double u, double v) {
    const double sc = 2.0 * u - 1.0, tc = 2.0 * v - 1.0;
    D3 d;
    switch (f) {
    case 0: d = {1.0, -tc, -sc}; break;
    case 1: d = {-1.0, -tc, sc}; break;
    case 2: d = {sc, 1.0, tc}; break;
    case 3: d = {sc, -1.0, -tc}; break;
    case 4: d = {sc, -tc, 1.0}; break;
    default: d = {-sc, -tc, -1.0}; break;
    }
    const double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return {d.x / l, d.y / l, d.z / l};
}

// The face texel a bilinear tap lands on. A tap past the face's edge goes through its
// texel-centre direction to the neighbouring face, so filtering is seamless (the GPU
// samplers of D3D10 and later do this in hardware; the clamped version left seams that
// flipped between faces under float32 rounding, 2026-10-10).
inline void tap(int n, int& f, int& y, int& x) {
    if (x >= 0 && x < n && y >= 0 && y < n) return;
    const D3 d = dirOf(f, (x + 0.5) / n, (y + 0.5) / n);
    double u, v;
    f = faceOf(d, u, v);
    x = std::clamp(int(u * n), 0, n - 1); y = std::clamp(int(v * n), 0, n - 1);
}

inline constexpr double kSunRadius = 0.0523598775598299;    // 3 degrees: resolvable on a 32-texel face
inline D3 sunDirection() { const double l = std::sqrt(0.09 + 0.36 + 0.5476); return {0.3 / l, 0.6 / l, -0.74 / l}; }

// Duff et al. 2017, "Building an Orthonormal Basis, Revisited".
inline void basis(D3 n, D3& t, D3& b) {
    const double s = n.z >= 0.0 ? 1.0 : -1.0, a = -1.0 / (s + n.z), c = n.x * n.y * a;
    t = {1.0 + s * n.x * n.x * a, s * c, -s * n.x};
    b = {c, s + n.y * n.y * a, -n.y};
}
inline double radicalInverse(std::uint32_t b) {
    b = (b << 16) | (b >> 16);
    b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
    b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
    b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
    b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
    return b * 2.3283064365386963e-10;
}
// GGX prefilter of one texel direction n (N = V = R): sum L(l) (n.l) / sum (n.l) over
// half vectors drawn from D, reading level 0 bilinearly.
inline Rgb prefilterTexel(const Cube& c, D3 n, double a, int samples) {
    D3 t, b;
    basis(n, t, b);
    double sr = 0, sg = 0, sb = 0, w = 0;
    for (int k = 0; k < samples; ++k) {
        const double u1 = (k + 0.5) / samples, u2 = radicalInverse(std::uint32_t(k));
        const double ct = std::sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1)), st = std::sqrt(std::max(0.0, 1.0 - ct * ct));
        const double ph = 2.0 * pbr::kPi * u2, hx = st * std::cos(ph), hy = st * std::sin(ph);
        const D3 h{t.x * hx + b.x * hy + n.x * ct, t.y * hx + b.y * hy + n.y * ct, t.z * hx + b.z * hy + n.z * ct};
        const double nh = n.x * h.x + n.y * h.y + n.z * h.z;
        const D3 l{2.0 * nh * h.x - n.x, 2.0 * nh * h.y - n.y, 2.0 * nh * h.z - n.z};
        const double nl = n.x * l.x + n.y * l.y + n.z * l.z;
        if (nl <= 0.0) continue;
        const Rgb v = c.sample(0, l);
        sr += v.r * nl; sg += v.g * nl; sb += v.b * nl; w += nl;
    }
    return {sr / w, sg / w, sb / w};
}
// Real spherical harmonics to order 2 (the 9 standard constants).
inline void shBasis(D3 d, double Y[9]) {
    Y[0] = 0.282094791773878;
    Y[1] = 0.488602511902920 * d.y; Y[2] = 0.488602511902920 * d.z; Y[3] = 0.488602511902920 * d.x;
    Y[4] = 1.092548430592079 * d.x * d.y; Y[5] = 1.092548430592079 * d.y * d.z;
    Y[6] = 0.315391565252520 * (3.0 * d.z * d.z - 1.0);
    Y[7] = 1.092548430592079 * d.x * d.z; Y[8] = 0.546274215296040 * (d.x * d.x - d.y * d.y);
}

}  // namespace raw::lighting::cube
