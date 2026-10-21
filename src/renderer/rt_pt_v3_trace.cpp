// Path tracer interface v3: one camera ray's sample (docs/rt-interface-v3.md). The v2 path in
// rt_pt_cpu.cpp is the template: the same random-number dimensions for the jitter and the BSDF,
// the same emitter MIS and Russian roulette, and the same split-radiance bookkeeping, with v3's
// punctual lights, environment and full materials added.
#include "rt_pt_v3.hpp"
#include <algorithm>
#include <cmath>
namespace raw::rt::v3 {
namespace {
using ptc::Surf;
using lighting::Rgb;
Rgb mulc(Rgb a, Rgb b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
Rgb sc(Rgb a, double k) { return {a.r * k, a.g * k, a.b * k}; }
Rgb plus(Rgb a, Rgb b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
double maxc(Rgb a) { return a.max3(); }
Vec3 offsetFor(const Surf& x, Vec3 dir) { return ptc::Ctx::offset(x, dir); }
const pbr::Material* fullOf(const PtScene& s, const Surf& x) {
    return s.fullMaterials.empty() ? nullptr : &s.fullMaterials[s.geo.triTexture[std::size_t(x.tri)]];
}
Rgb emissionOf(const PtScene& s, const Surf& x) {
    if (const pbr::Material* m = fullOf(s, x)) return pbr::emission(*m);
    return {x.m->emission.x, x.m->emission.y, x.m->emission.z};
}
// Light gathered at x by next-event estimation: punctual lights, the sun, one emitter sample and
// one environment sample (MIS against the BSDF). smooth receives the diffuse AOV's share.
Rgb nee(const ptc::Ctx& C, const PtScene& s, const PathTraceDesc& d, const Surf& x, const Shade& h, D3 wo,
        std::uint32_t px, std::uint32_t smp, std::uint32_t base, Rgb& smooth) {
    Rgb L{}; smooth = {};
    const auto add = [&](D3 l, Rgb radiance, double weight, double tmax) {
        const Eval e = eval(s, h, wo, toLocal(h, l));
        if (maxc(e.fcos) <= 0.0) return;
        const Vec3 lf = f3(l);
        if (!C.visible(offsetFor(x, lf), lf, float(tmax))) return;
        L = plus(L, sc(mulc(e.fcos, radiance), weight));
        smooth = plus(smooth, sc(mulc(e.smooth, radiance), weight));
    };
    for (const lighting::Light& li : s.lights) {
        D3 to;
        const Rgb E = lighting::illuminance(li, d3(x.p), to);
        if (maxc(E) <= 0.0) continue;
        double dist = 1e30;
        if (li.type != lighting::LightType::Directional) {
            const D3 q{li.position.x - x.p.x, li.position.y - x.p.y, li.position.z - x.p.z};
            dist = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z) * (1.0 - 1e-4);
        }
        add(to, E, 1.0, dist);
    }
    if (ptc::maxc(s.sunIrradiance) > 0.0f) add(d3(normalize(s.sunDir)), {s.sunIrradiance.x, s.sunIrradiance.y, s.sunIrradiance.z}, 1.0, 1e30);
    if (!s.emitters.empty()) {
        const pt::R2 a = pt::rand2(d.seed, px, smp, base + 2), b = pt::rand2(d.seed, px, smp, base + 3);
        std::size_t k = std::size_t(std::upper_bound(s.emitterCdf.begin(), s.emitterCdf.end(), a.a * s.emitterArea) - s.emitterCdf.begin());
        k = std::min(k, s.emitters.size() - 1);
        const Tri& t = C.tris[s.emitters[k]];
        const float su = std::sqrt(a.b);
        const Vec3 q = t.a * (1.0f - su) + t.b * (su * (1.0f - b.a)) + t.c * (su * b.a), to = q - x.p;
        const float dist = length(to);
        const Vec3 l = to * (1.0f / dist);
        const float cosL = std::fabs(dot(normalize(cross(t.b - t.a, t.c - t.a)), l));
        if (cosL > 0.0f) {
            const double pl = double(dist) * dist / (double(cosL) * s.emitterArea), pb = eval(s, h, wo, toLocal(h, d3(l))).pdf;
            const double w = d.controls.misWeightOne ? 1.0 : pl * pl / (pl * pl + pb * pb);
            Surf e; e.tri = int(s.emitters[k]); e.m = &s.materials[s.geo.triTexture[s.emitters[k]]];
            add(d3(l), emissionOf(s, e), w / pl, dist * (1.0f - 1e-3f));
        }
    }
    if (!s.envCdf.empty() && !d.controls.envNoNee) {
        const pt::R2 u = pt::rand2(d.seed, px, smp, base + 4);   // dimensions base + 4 and up are v3's own
        D3 l; double pe;
        if (envSample(s, u.a, u.b, l, pe)) {
            const double pb = eval(s, h, wo, toLocal(h, l)).pdf;
            const double w = d.controls.envMisWeightOne ? 1.0 : pe * pe / (pe * pe + pb * pb);
            add(l, envRadiance(s, l), w / pe, 1e30);
        }
    }
    return L;
}
Vec3 v3f(Rgb c) { return {float(c.r), float(c.g), float(c.b)}; }
}  // namespace

ptc::Sample tracePath(const ptc::Ctx& C, const PtScene& s, const PathTraceDesc& d, Ray ray, std::uint32_t px, std::uint32_t smp) {
    ptc::Sample o;
    Rgb L{}, T{1, 1, 1}, tw{1, 1, 1}, R{}, wd{}, ws{}, D{}, S{}, E{};
    double prevPdf = 0.0;
    const bool env = !s.environment.rgb.empty();
    for (std::uint32_t b = 0; b <= d.maxBounces; ++b) {
        Surf x;
        if (!C.hit(ray, 1e30f, x)) {
            Rgb Le = env ? envRadiance(s, d3(ray.d)) : Rgb{s.sky.x, s.sky.y, s.sky.z};
            if (env && b > 0 && !d.controls.envNoNee && !d.controls.envMisWeightOne) {
                const double pe = envPdf(s, d3(ray.d));
                Le = sc(Le, prevPdf * prevPdf / (prevPdf * prevPdf + pe * pe));
            }
            L = plus(L, mulc(T, Le));
            if (b == 0) E = Le; else R = plus(R, mulc(tw, Le));
            break;
        }
        const pbr::Material* full = fullOf(s, x);
        if (x.back && full && full->volume && full->attenuationDistance > 0.0) {   // the segment just travelled was inside
            const Rgb c = full->attenuationColor;
            const double k = double(x.t) / full->attenuationDistance;
            const Rgb att{std::pow(std::max(c.r, 1e-300), k), std::pow(std::max(c.g, 1e-300), k), std::pow(std::max(c.b, 1e-300), k)};
            T = mulc(T, att); if (b > 0) tw = mulc(tw, att);
        }
        const Shade h = shadeAt(s, x, full);
        if (b == 0) {
            o.alb = full ? v3f(h.m.baseColor) : x.base; o.nrm = x.n;
            o.dAlb = full ? v3f(sc(h.m.baseColor, 1.0 - std::clamp(h.m.metallic, 0.0, 1.0))) : x.base * (1.0f - x.m->metallic);
            o.sAlb = full ? Vec3{0, 0, 0} : pt::specAlbedo(h.v2, std::clamp(-dot(x.n, ray.d), 0.0f, 1.0f));
        }
        const Rgb Le = emissionOf(s, x);
        if (maxc(Le) > 0.0) {
            double w = 1.0;
            if (b > 0 && !d.controls.misWeightOne && s.emitterArea > 0.0f) {
                const double cosL = std::fabs(dot(x.ng, ray.d)), pl = double(x.t) * x.t / (cosL * s.emitterArea);
                w = prevPdf * prevPdf / (prevPdf * prevPdf + pl * pl);
            }
            L = plus(L, sc(mulc(T, Le), w));
            if (b == 0) E = Le; else R = plus(R, sc(mulc(tw, Le), w));
        }
        if (b == d.maxBounces) break;
        const D3 wo = toLocal(h, d3(ray.d * -1.0f));
        if (wo.z <= 0.0) break;
        const std::uint32_t base = 2 + b * 6;
        Rgb nd;
        const Rgb Ln = nee(C, s, d, x, h, wo, px, smp, base, nd);
        L = plus(L, mulc(T, Ln));
        if (b == 0) { D = nd; S = plus(Ln, sc(nd, -1.0)); } else R = plus(R, mulc(tw, Ln));
        const pt::R2 u = pt::rand2(d.seed, px, smp, base), r = pt::rand2(d.seed, px, smp, base + 1);
        D3 wi;
        if (!sample(h, wo, r.a, u.a, u.b, wi)) break;
        const Eval e = eval(s, h, wo, wi);
        if (e.pdf <= 0.0 || maxc(e.fcos) <= 0.0) break;
        const Rgb step = sc(e.fcos, 1.0 / e.pdf);
        T = mulc(T, step);
        if (b == 0) { wd = sc(e.smooth, 1.0 / e.pdf); ws = plus(step, sc(wd, -1.0)); } else tw = mulc(tw, step);
        prevPdf = e.pdf;
        if (d.russianRoulette && b >= 3) {
            const double q = std::min(0.95, maxc(T));
            if (r.b >= q) break;
            T = sc(T, 1.0 / q); tw = sc(tw, 1.0 / q);
        }
        const Vec3 wv = f3(toWorld(h, wi));
        ray = {ptc::Ctx::offset(x, wv), normalize(wv)};
    }
    D = plus(D, mulc(wd, R));
    if (!d.controls.aovDropSpecularIndirect) S = plus(S, mulc(ws, R));
    if (d.controls.aovEmissionToDiffuse) { D = plus(D, E); E = {}; }
    o.L = v3f(L); o.D = v3f(D); o.S = v3f(S); o.E = v3f(E);
    return o;
}

}  // namespace raw::rt::v3
