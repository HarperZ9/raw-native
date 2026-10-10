// Image lighting of a sample: the split sum, and the reference it approximates.
// See raw/renderer/lighting.hpp.
#include "raw/renderer/lighting.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace raw::lighting {
namespace {
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 toWorld(const Sample& s, D3 w) {
    const D3 b = cross(s.n, s.t);
    return {s.t.x * w.x + b.x * w.y + s.n.x * w.z, s.t.y * w.x + b.y * w.y + s.n.y * w.z, s.t.z * w.x + b.z * w.y + s.n.z * w.z};
}
Rgb madd(Rgb acc, Rgb w, Rgb l) { return {acc.r + w.r * l.r, acc.g + w.g * l.g, acc.b + w.b * l.b}; }
// The direction the prefiltered environment is read along: the centroid of the lobe f cos,
// at elevation Tables::Dom(n.v, roughness) from the normal, toward the mirror direction.
// History (2026-10-10): the mirror direction failed the split-sum bound (p95 0.40); the
// Frostbite dominant-direction fit (Lagarde and de Rousiers 2014) reached 0.181 on a cube
// with 1-texel rough levels and 0.209 once those levels kept 8 texels; the centroid table
// replaces the fit with the BRDF's own lobe, computed from the model and not from the test.
D3 dominant(const pbr::Tables& t, D3 n, D3 v, double roughness) {
    const double nv = std::clamp(dot(n, v), 0.0, 1.0), e = t.Dom(nv, roughness);
    const D3 r{2.0 * nv * n.x - v.x, 2.0 * nv * n.y - v.y, 2.0 * nv * n.z - v.z};
    const double rn = dot(r, n);
    const D3 p{r.x - rn * n.x, r.y - rn * n.y, r.z - rn * n.z};          // the mirror direction's tangential part
    const double pl = std::sqrt(dot(p, p));
    if (pl < 1e-12) return n;
    const double c = std::cos(e), s = std::sin(e);
    return {n.x * c + p.x / pl * s, n.y * c + p.y / pl * s, n.z * c + p.z / pl * s};
}
// The transmitted direction: straight through for a thin wall, refracted for a volume.
D3 transmitted(const Sample& s) {
    const D3 i{-s.v.x, -s.v.y, -s.v.z};
    if (!s.m.volume) return i;
    const double eta = 1.0 / s.m.ior, c = -dot(s.n, i), k = 1.0 - eta * eta * (1.0 - c * c);
    if (k < 0.0) return {i.x - 2.0 * dot(i, s.n) * s.n.x, i.y - 2.0 * dot(i, s.n) * s.n.y, i.z - 2.0 * dot(i, s.n) * s.n.z};
    const double q = eta * c - std::sqrt(k);
    return {eta * i.x + q * s.n.x, eta * i.y + q * s.n.y, eta * i.z + q * s.n.z};
}
}  // namespace

Rgb shadeIbl(const Sample& s, const pbr::Tables& t, const Cube& c, const std::vector<Rgb>& sh) {
    const pbr::IblResponse r = pbr::iblResponse(s.m, t, toLocal(s, s.v));
    const D3 refl = dominant(t, s.n, s.v, r.roughness);
    Rgb out = madd({}, r.specular, c.sampleRough(r.roughness, refl));
    out = madd(out, r.irradiance, shIrradiance(sh, s.n));
    if (r.transmission.max3() > 0.0) out = madd(out, r.transmission, c.sampleRough(r.roughness, transmitted(s)));
    if (r.coat.max3() > 0.0) {
        const D3 cn0 = toWorld(s, s.m.coatNormal);
        const double cl = std::sqrt(dot(cn0, cn0));
        const D3 cn{cn0.x / cl, cn0.y / cl, cn0.z / cl};
        out = madd(out, r.coat, c.sampleRough(r.coatRoughness, dominant(t, cn, s.v, r.coatRoughness)));
    }
    return out;
}

// Multiple importance sampling over three strategies with the balance heuristic: GGX VNDF
// (Heitz 2018) reflection, cosine hemisphere, and the environment's texels by luminance.
Rgb shadeIblReference(const Sample& s, const pbr::Tables& t, const Cube& c, int n) {
    const int N = c.size;
    std::vector<double> cdf(std::size_t(6) * N * N);
    double total = 0.0;
    for (int i = 0; i < 6 * N * N; ++i) {
        const int f = i / (N * N), y = (i / N) % N, x = i % N;
        const double sc = 2.0 * (x + 0.5) / N - 1.0, tc = 2.0 * (y + 0.5) / N - 1.0;
        const Rgb L = c.texel(0, f, y, x);
        total += (0.2126 * L.r + 0.7152 * L.g + 0.0722 * L.b + 1e-6) / std::pow(1.0 + sc * sc + tc * tc, 1.5);
        cdf[std::size_t(i)] = total;
    }
    const auto pdfEnv = [&](D3 d) {
        double u, v; const int f = cube::faceOf(d, u, v);
        const int x = std::min(int(u * N), N - 1), y = std::min(int(v * N), N - 1), i = (f * N + y) * N + x;
        const double p = (cdf[std::size_t(i)] - (i ? cdf[std::size_t(i - 1)] : 0.0)) / total, sc = 2 * u - 1, tc = 2 * v - 1;
        return p * (N * 0.5) * (N * 0.5) * std::pow(1.0 + sc * sc + tc * tc, 1.5);
    };
    const D3 wo = toLocal(s, s.v);
    const double a = pbr::alphaOf(s.m.roughness);
    const double lo = 0.5 * (-1.0 + std::sqrt(1.0 + a * a * (1.0 - wo.z * wo.z) / (wo.z * wo.z))), g1 = 1.0 / (1.0 + lo);
    const auto pdfGgx = [&](D3 wi) {
        if (wi.z <= 0.0) return 0.0;
        D3 h{wo.x + wi.x, wo.y + wi.y, wo.z + wi.z};
        const double l = std::sqrt(dot(h, h));
        h = {h.x / l, h.y / l, h.z / l};
        return g1 * pbr::ggxD(h, a, a) / (4.0 * wo.z);
    };
    Rgb sum;
    for (int strat = 0; strat < 3; ++strat) for (int k = 0; k < n; ++k) {
        const double u1 = std::fmod((k + 0.5) / n + 0.37 * strat, 1.0), u2 = std::fmod(cube::radicalInverse(std::uint32_t(k)) + 0.61 * strat, 1.0);
        D3 wl;
        if (strat == 0) {
            const D3 vh = [&] { const D3 q{a * wo.x, a * wo.y, wo.z}; const double l = std::sqrt(dot(q, q)); return D3{q.x / l, q.y / l, q.z / l}; }();
            const double lsq = vh.x * vh.x + vh.y * vh.y;
            const D3 t1 = lsq > 0 ? D3{-vh.y / std::sqrt(lsq), vh.x / std::sqrt(lsq), 0} : D3{1, 0, 0}, t2 = cross(vh, t1);
            const double r = std::sqrt(u1), ph = 2 * pbr::kPi * u2, sp = 0.5 * (1 + vh.z);
            const double p1 = r * std::cos(ph), p2 = (1 - sp) * std::sqrt(1 - p1 * p1) + sp * r * std::sin(ph), p3 = std::sqrt(std::max(0.0, 1 - p1 * p1 - p2 * p2));
            D3 h{a * (t1.x * p1 + t2.x * p2 + vh.x * p3), a * (t1.y * p1 + t2.y * p2 + vh.y * p3), std::max(1e-12, t1.z * p1 + t2.z * p2 + vh.z * p3)};
            const double hl = std::sqrt(dot(h, h)); h = {h.x / hl, h.y / hl, h.z / hl};
            const double oh = dot(wo, h);
            wl = {2 * oh * h.x - wo.x, 2 * oh * h.y - wo.y, 2 * oh * h.z - wo.z};
        } else if (strat == 1) {
            const double r = std::sqrt(u1), ph = 2 * pbr::kPi * u2;
            wl = {r * std::cos(ph), r * std::sin(ph), std::sqrt(std::max(0.0, 1 - u1))};
        } else {
            const double target = u1 * total;
            const int i = int(std::lower_bound(cdf.begin(), cdf.end(), target) - cdf.begin());
            const int f = i / (N * N), y = (i / N) % N, x = i % N;
            const double lo0 = i ? cdf[std::size_t(i - 1)] : 0.0, rem = (target - lo0) / (cdf[std::size_t(i)] - lo0);   // reused as the second uniform
            wl = toLocal(s, cube::dirOf(f, (x + u2) / N, (y + std::clamp(rem, 0.0, 1.0)) / N));
        }
        if (wl.z <= 0.0) continue;
        const D3 ww = toWorld(s, wl);
        const double pdf = pdfGgx(wl) + wl.z / pbr::kPi + pdfEnv(ww);
        const pbr::Rgb f = pbr::eval(s.m, t, wo, wl), L = c.sample(0, ww);
        const double w = wl.z / (pdf * n);
        sum = {sum.r + f.r * L.r * w, sum.g + f.g * L.g * w, sum.b + f.b * L.b * w};
    }
    return sum;
}

}  // namespace raw::lighting
