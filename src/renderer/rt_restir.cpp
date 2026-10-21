// ReSTIR DI on the CPU (raw/renderer/rt_restir.hpp).
#include "raw/renderer/rt_restir.hpp"
#include "raw/core/parallel.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "rt_pt_v3.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::rt {
namespace {
using lighting::Rgb;
using v3::D3;
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
// Counter-based uniforms for (seed, pixel, stream, index): the path tracer's PCG2D.
double rnd(std::uint64_t seed, std::uint32_t px, std::uint32_t stream, std::uint32_t i) {
    const pt::R2 r = pt::rand2(seed, px, stream * 8192u + i / 2u, 40u);
    return (i & 1u) ? r.b : r.a;
}
// The primary hit of one pixel and what shading at it needs.
struct Px {
    bool hit{false};
    ptc::Surf x;
    v3::Shade h;
    D3 wo;
};
// The unshadowed contribution of light l at the pixel, and its luminance (the target p_hat).
Rgb contribution(const PtScene& s, const Px& p, int l, D3& toLight, double& dist) {
    const lighting::Light& li = s.lights[std::size_t(l)];
    const Rgb E = lighting::illuminance(li, v3::d3(p.x.p), toLight);
    dist = 1e30;
    if (li.type != lighting::LightType::Directional) {
        const D3 q{li.position.x - p.x.p.x, li.position.y - p.x.p.y, li.position.z - p.x.p.z};
        dist = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z) * (1.0 - 1e-4);
    }
    if (E.max3() <= 0.0) return {};
    const v3::Eval e = v3::eval(s, p.h, p.wo, v3::toLocal(p.h, toLight));
    return {e.fcos.r * E.r, e.fcos.g * E.g, e.fcos.b * E.b};
}
double pHat(const PtScene& s, const Px& p, int l) { D3 t; double d; return p.hit && l >= 0 ? lum(contribution(s, p, l, t, d)) : 0.0; }
Rgb shade(const ptc::Ctx& C, const PtScene& s, const Px& p, int l, double weight, std::uint64_t& rays) {
    if (l < 0 || weight <= 0.0) return {};
    D3 to; double dist;
    const Rgb c = contribution(s, p, l, to, dist);
    if (c.max3() <= 0.0) return {};
    ++rays;
    const Vec3 lf = v3::f3(to);
    if (!C.visible(ptc::Ctx::offset(p.x, lf), lf, float(dist))) return {};
    return {c.r * weight, c.g * weight, c.b * weight};
}
struct Reservoir { int y{-1}; double wsum{0}, M{0}, W{0}; };
bool update(Reservoir& r, int y, double w, double M, double u) {
    r.wsum += w; r.M += M;
    if (w > 0.0 && u * r.wsum < w) { r.y = y; return true; }
    return false;
}
Vec3 dirOf(const PtCamera& c, int w, int h, float px, float py) {
    const Vec3 f = normalize(c.target - c.eye), sd = normalize(cross(f, c.up)), u = cross(sd, f);
    const float th = std::tan(c.fovy * 0.5f), aspect = float(w) / float(h);
    const float nx = px / float(w) * 2.0f - 1.0f, ny = 1.0f - py / float(h) * 2.0f;
    return normalize(f + sd * (nx * th * aspect) + u * (ny * th));
}
}  // namespace

std::vector<lighting::Light> scatterLights(int n, Vec3 lo, Vec3 hi, std::uint64_t seed, double i0, double range) {
    std::vector<lighting::Light> out;
    std::uint64_t z = seed * 6364136223846793005ull + 1442695040888963407ull;
    const auto next = [&] { z = z * 6364136223846793005ull + 1442695040888963407ull; return double(z >> 11) * (1.0 / 9007199254740992.0); };
    for (int i = 0; i < n; ++i) {
        lighting::Light l;
        l.type = lighting::LightType::Point;
        l.position = {lo.x + (hi.x - lo.x) * next(), lo.y + (hi.y - lo.y) * next(), lo.z + (hi.z - lo.z) * next()};
        l.intensity = i0 * std::pow(30.0, next());
        l.color = {0.5 + 0.5 * next(), 0.5 + 0.5 * next(), 0.5 + 0.5 * next()};
        l.range = range;
        out.push_back(l);
    }
    return out;
}

DiOutput directLightCpu(const PtScene& s, const DiDesc& d) {
    DiOutput O;
    O.width = d.width; O.height = d.height;
    const int W = d.width, H = d.height, N = int(s.lights.size());
    const std::size_t n = std::size_t(W) * std::size_t(H);
    O.radiance.assign(n * 3, 0.0f); O.covered.assign(n, 0);
    if (N == 0) { O.error = "directLightCpu: the scene has no punctual lights"; return O; }
    PathTraceDesc pd; pd.seed = d.seed;
    const ptc::Ctx C{s, pd, trianglesOf(s.geo)};
    const int threads = d.threads > 0 ? d.threads : int(std::max(1u, std::thread::hardware_concurrency()));
    std::vector<Px> px(n);
    std::vector<Reservoir> res(n);
    std::vector<std::uint64_t> rowRays(std::size_t(H), 0);
    const int M = std::max(1, d.candidates);
    // Pass 1: primary hits and, for the resampling modes, each pixel's own reservoir.
    parallelRows(H, threads, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t i = std::size_t(y) * W + x;
            Px& p = px[i];
            const Ray r{d.camera.eye, dirOf(d.camera, W, H, float(x) + 0.5f, float(y) + 0.5f)};
            if (!C.hit(r, 1e30f, p.x)) continue;
            const pbr::Material* full = s.fullMaterials.empty() ? nullptr : &s.fullMaterials[s.geo.triTexture[std::size_t(p.x.tri)]];
            p.h = v3::shadeAt(s, p.x, full);
            p.wo = v3::toLocal(p.h, v3::d3(r.d * -1.0f));
            p.hit = p.wo.z > 0.0;
            O.covered[i] = 1;
            if (!p.hit || d.mode == DiMode::Exact || d.mode == DiMode::Uniform) continue;
            Reservoir& q = res[i];
            for (int k = 0; k < M; ++k) {
                const int l = std::min(N - 1, int(rnd(d.seed, std::uint32_t(i), 0, std::uint32_t(2 * k)) * N));
                update(q, l, pHat(s, p, l) * N, 1.0, rnd(d.seed, std::uint32_t(i), 0, std::uint32_t(2 * k + 1)));   // w = p_hat / (1 / N)
            }
            const double ph = pHat(s, p, q.y);
            q.W = ph > 0.0 ? q.wsum / ((d.controlDropOneOverM ? 1.0 : q.M) * ph) : 0.0;
        }
    });
    // Pass 2: the spatial pass (Restir modes) and shading, one shadow ray a pixel.
    parallelRows(H, threads, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t i = std::size_t(y) * W + x;
            const Px& p = px[i];
            if (!p.hit) continue;
            Rgb L{};
            std::uint64_t& rays = rowRays[std::size_t(y)];
            const std::uint32_t pi = std::uint32_t(i);
            if (d.mode == DiMode::Exact) {
                for (int l = 0; l < N; ++l) { const Rgb c = shade(C, s, p, l, 1.0, rays); L = {L.r + c.r, L.g + c.g, L.b + c.b}; }
            } else if (d.mode == DiMode::Uniform) {
                L = shade(C, s, p, std::min(N - 1, int(rnd(d.seed, pi, 1, 0) * N)), double(N), rays);
            } else if (d.mode == DiMode::Ris) {
                L = shade(C, s, p, res[i].y, res[i].W, rays);
            } else {
                std::vector<std::size_t> set{i};
                for (int k = 0; k < d.neighbours; ++k) {
                    const double a = 2.0 * pbr::kPi * rnd(d.seed, pi, 2, std::uint32_t(2 * k)), rr = d.radius * std::sqrt(rnd(d.seed, pi, 2, std::uint32_t(2 * k + 1)));
                    const int nx = x + int(std::lround(rr * std::cos(a))), ny = y + int(std::lround(rr * std::sin(a)));
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const std::size_t j = std::size_t(ny) * W + nx;
                    if (j == i || !px[j].hit) continue;
                    if (dot(px[j].x.n, p.x.n) < 0.906f || std::fabs(px[j].x.t - p.x.t) > 0.1f * p.x.t) continue;   // normals within 25 degrees, depth within 10% (Bitterli et al. 2020); a selection by pixel data, so Z stays consistent
                    set.push_back(j);
                }
                Reservoir r;
                for (std::size_t t = 0; t < set.size(); ++t) {
                    const Reservoir& q = res[set[t]];
                    update(r, q.y, pHat(s, p, q.y) * q.W * q.M, q.M, rnd(d.seed, pi, 3, std::uint32_t(t)));
                }
                const double ph = pHat(s, p, r.y);
                double Z = r.M;
                if (d.mode == DiMode::Restir) {
                    Z = 0.0;
                    for (std::size_t t : set) if (pHat(s, px[t], r.y) > 0.0) Z += res[t].M;
                }
                L = shade(C, s, p, r.y, (ph > 0.0 && Z > 0.0) ? r.wsum / (Z * ph) : 0.0, rays);
            }
            O.radiance[i * 3] = float(L.r); O.radiance[i * 3 + 1] = float(L.g); O.radiance[i * 3 + 2] = float(L.b);
        }
    });
    for (std::uint64_t r : rowRays) O.shadowRays += r;
    return O;
}

}  // namespace raw::rt
