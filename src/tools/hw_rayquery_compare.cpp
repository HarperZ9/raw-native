// HW H1.1 classification (hw_rayquery_compare.hpp; definitions in evidence/hw-h1-1-bounds.json).
#include "hw_rayquery_compare.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw {
namespace {
float maxAbs(Vec3 p){ return std::max({std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)}); }
float L(const rhi::hw::RayIn& r, const Tri& t){ return std::max({maxAbs({r.ox, r.oy, r.oz}), maxAbs(t.a), maxAbs(t.b), maxAbs(t.c)}); }
float minAltitude(const Tri& t){
    const float area2 = length(cross(t.b - t.a, t.c - t.a));
    const float e = std::max({length(t.b - t.a), length(t.c - t.b), length(t.a - t.c)});
    return e > 0 ? area2 / e : 0;
}
double tTol(double t, double l){ return 1e-5 * t + std::ldexp(l, -20); }
double uvTol(double l, double h){ return h > 0 ? 1e-4 + std::ldexp(l, -18) / h : 1e30; }
double cpuDet(const rhi::hw::RayIn& r, const Tri& t){ return dot(t.b - t.a, cross(Vec3{r.dx, r.dy, r.dz}, t.c - t.a)); }
bool nearEdge(double u, double v, double tol){ return std::min({u, v, 1.0 - u - v}) <= tol; }
}  // namespace

void RayCompare::add(const RayCompare& o){
    rays += o.rays; bothMiss += o.bothMiss; sameTri += o.sameTri; violT += o.violT; violUV += o.violUV;
    edge += o.edge; tie += o.tie; grazing += o.grazing; boundary += o.boundary; unexplained += o.unexplained;
    worstT = std::max(worstT, o.worstT); worstUV = std::max(worstUV, o.worstUV);
    for (const auto& s : o.samples) if (samples.size() < 8) samples.push_back(s);
}

RayCompare compareHits(const std::vector<Tri>& tris, const std::vector<rhi::hw::RayIn>& rays,
                       const std::vector<Hit>& cpu, const std::vector<bool>& cpuHit, const std::vector<rhi::hw::HitOut>& gpu){
    RayCompare c;
    c.rays = rays.size();
    for (size_t i = 0; i < rays.size(); ++i){
        const auto& r = rays[i];
        const bool ch = cpuHit[i], gh = gpu[i].tri >= 0 && (size_t)gpu[i].tri < tris.size();
        if (!ch && !gh){ ++c.bothMiss; continue; }
        if (ch && gh && cpu[i].tri == gpu[i].tri){
            const Tri& t = tris[(size_t)cpu[i].tri];
            const double l = L(r, t), tt = tTol(cpu[i].t, l), ut = uvTol(l, minAltitude(t));
            const double et = std::fabs((double)gpu[i].t - cpu[i].t) / tt;
            const double eu = std::max(std::fabs((double)gpu[i].u - cpu[i].u), std::fabs((double)gpu[i].v - cpu[i].v)) / ut;
            c.worstT = std::max(c.worstT, et); c.worstUV = std::max(c.worstUV, eu);
            if (et > 1) ++c.violT;
            if (eu > 1) ++c.violUV;
            if ((et > 1 || eu > 1) && c.samples.size() < 8){
                char b[256];
                std::snprintf(b, sizeof b, "ray %zu tolerance: tri %d cpu t %.9g uv (%.7g, %.7g) gpu t %.9g uv (%.7g, %.7g); t %.3g, uv %.3g of tolerance; |det| %.3g, L %.3g, h %.3g",
                              i, cpu[i].tri, cpu[i].t, cpu[i].u, cpu[i].v, gpu[i].t, gpu[i].u, gpu[i].v, et, eu, std::fabs(cpuDet(r, t)), l, minAltitude(t));
                c.samples.push_back(b);
            }
            ++c.sameTri;
            continue;
        }
        // A disagreement: try each explanation in the committed order.
        bool done = false;
        auto edgeOf = [&](int tri, double u, double v){
            const Tri& t = tris[(size_t)tri];
            return nearEdge(u, v, uvTol(L(r, t), minAltitude(t)));
        };
        if (ch && edgeOf(cpu[i].tri, cpu[i].u, cpu[i].v)){ ++c.edge; done = true; }
        else if (gh && edgeOf(gpu[i].tri, gpu[i].u, gpu[i].v)){ ++c.edge; done = true; }
        if (!done && ch && gh){
            const double l = std::max(L(r, tris[(size_t)cpu[i].tri]), L(r, tris[(size_t)gpu[i].tri]));
            if (std::fabs((double)gpu[i].t - cpu[i].t) <= tTol(std::max<double>(cpu[i].t, gpu[i].t), l)){ ++c.tie; done = true; }
        }
        if (!done && gh && std::fabs(cpuDet(r, tris[(size_t)gpu[i].tri])) < 1e-6){ ++c.grazing; done = true; }
        if (!done){
            auto atBound = [&](double t, int tri){ const double tol = tTol(t, L(r, tris[(size_t)tri])); return std::fabs(t - r.tmin) <= tol || std::fabs(t - r.tmax) <= tol; };
            if ((ch && atBound(cpu[i].t, cpu[i].tri)) || (gh && atBound(gpu[i].t, gpu[i].tri))){ ++c.boundary; done = true; }
        }
        if (!done){
            ++c.unexplained;
            if (c.samples.size() < 8){
                char b[256];
                std::snprintf(b, sizeof b, "ray %zu: cpu %s tri %d t %.9g uv (%.6g, %.6g); gpu %s tri %d t %.9g uv (%.6g, %.6g)", i,
                              ch ? "hit" : "miss", ch ? cpu[i].tri : -1, ch ? cpu[i].t : 0.0f, ch ? cpu[i].u : 0.0f, ch ? cpu[i].v : 0.0f,
                              gh ? "hit" : "miss", gpu[i].tri, gpu[i].t, gpu[i].u, gpu[i].v);
                c.samples.push_back(b);
            }
        }
    }
    return c;
}
}  // namespace raw
