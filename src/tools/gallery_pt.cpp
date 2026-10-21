// The material gallery's path-traced error: see raw/tools/gallery_pt.hpp.
#include "raw/tools/gallery_pt.hpp"
#include "raw/renderer/rt_pathtrace.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
namespace raw::gallery {
namespace {
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
// One UV sphere, outward winding, on texture `tex`.
void sphere(swr::Geometry& g, lighting::D3 c, double r, int nu, int nv, std::uint32_t tex) {
    const std::uint32_t o = std::uint32_t(g.pos.size());
    const double pi = pbr::kPi;
    for (int j = 0; j <= nv; ++j) for (int i = 0; i <= nu; ++i) {
        const double th = pi * j / nv, ph = 2.0 * pi * i / nu;
        const Vec3 n{float(std::sin(th) * std::cos(ph)), float(std::cos(th)), float(std::sin(th) * std::sin(ph))};
        g.pos.push_back({float(c.x + r * n.x), float(c.y + r * n.y), float(c.z + r * n.z)});
        g.nrm.push_back(n); g.uv.push_back({0.5f, 0.5f});
    }
    const Vec3 cf{float(c.x), float(c.y), float(c.z)};
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i) {
        const std::uint32_t a = o + std::uint32_t(j * (nu + 1) + i), b = a + 1, cc = a + std::uint32_t(nu + 1), d = cc + 1;
        std::uint32_t tri[2][3] = {{a, cc, d}, {a, d, b}};
        for (auto& q : tri) {
            const Vec3 A = g.pos[q[0]], B = g.pos[q[1]], C = g.pos[q[2]];
            if (length(cross(B - A, C - A)) <= 0.0f) continue;   // the poles' slivers
            if (dot(cross(B - A, C - A), (A + B + C) * (1.0f / 3.0f) - cf) < 0.0f) std::swap(q[1], q[2]);
            for (std::uint32_t k : q) g.idx.push_back(k);
            g.triTexture.push_back(tex);
        }
    }
}
rt::PtScene sceneOf(const Gallery& g, const PtErrorDesc& d) {
    rt::PtScene s;
    s.name = g.name;
    for (const Gallery::Sphere& sp : g.spheres) {
        const std::uint32_t tex = s.tex.add(1, 1, {0xffffffffu});
        s.materials.push_back({1.0f, 1.0f, 0.0f, 0.0f, {}});
        s.fullMaterials.push_back(sp.m);
        sphere(s.geo, sp.centre, sp.radius, d.nu, d.nv, tex);
    }
    if (g.ground) {   // the raster's ground quad, in view space
        const std::uint32_t tex = s.tex.add(1, 1, {0xffffffffu}), o = std::uint32_t(s.geo.pos.size());
        s.materials.push_back({1.0f, 1.0f, 0.0f, 0.0f, {}});
        s.fullMaterials.push_back(g.groundMaterial);
        const Vec3 n{float(g.groundNormal.x), float(g.groundNormal.y), float(g.groundNormal.z)};
        for (const lighting::D3& q : g.groundQuad) { s.geo.pos.push_back({float(q.x), float(q.y), float(q.z)}); s.geo.nrm.push_back(n); s.geo.uv.push_back({0.5f, 0.5f}); }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) s.geo.idx.push_back(o + i);   // counter-clockwise seen from the front
        s.geo.triTexture.push_back(tex); s.geo.triTexture.push_back(tex);
    }
    s.lights = g.lights;
    s.environment = g.env;
    for (double& v : s.environment.rgb) v *= d.envScale;
    rt::finishPtScene(s);
    return s;
}
}  // namespace

std::string ptError(const Gallery& g, const std::vector<Rgb>& cpuPerSample, const pbr::Tables& t, const lighting::Cube& prefiltered,
                    const PtErrorDesc& d, std::vector<Rgb>& ptImage) {
    const auto t0 = std::chrono::steady_clock::now();
    const rt::PtScene s = sceneOf(g, d);
    // The tracer samples the raster's own points: an image k times the size (k = the raster's
    // samples a pixel along each axis), unjittered, so each tracer pixel centre is one raster
    // sub-sample; k x k blocks are then averaged. Point-sampled sub-pixel highlights then alias
    // the same way on both sides, and the comparison measures shading, not the sample pattern.
    const int W = g.width, H = g.height, k = g.spp, full = k * k;
    rt::PathTraceDesc p;
    p.width = W * k; p.height = H * k; p.spp = std::uint32_t(d.spp); p.maxBounces = std::uint32_t(d.bounces); p.seed = 1;
    p.jitter = false;
    p.camera = p.previous = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, float(g.fovy)};
    const rt::PathTraceOutput o = rt::pathTraceCpu(s, p, d.threads);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const std::vector<Rgb> raster = resolve(g, cpuPerSample);
    ptImage.assign(std::size_t(W) * H, {});
    long both = 0, agree = 0;
    double sr = 0, sp = 0, sd = 0, sd2 = 0, sv = 0;
    const std::size_t G = g.groupLabels.size();
    std::vector<double> rowD2(G), rowP(G), rowD(G); std::vector<long> rowN(G);
    std::vector<int> pixelGroup(std::size_t(W) * H, -2);   // -2: no sample, -1: samples of two groups
    for (std::size_t q = 0; q < g.samples.size() && q < g.group.size(); ++q) {
        int& pg = pixelGroup[std::size_t(g.pixel[q])];
        pg = pg == -2 ? g.group[q] : (pg == g.group[q] ? pg : -1);
    }
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const std::size_t i = std::size_t(y) * W + x;
        Rgb acc{}; int hits = 0; double var = 0;
        for (int sy = 0; sy < k; ++sy) for (int sx = 0; sx < k; ++sx) {
            const std::size_t j = std::size_t(y * k + sy) * std::size_t(W * k) + std::size_t(x * k + sx);
            const float* q = o.radiance.data() + 3 * j;
            acc = {acc.r + q[0], acc.g + q[1], acc.b + q[2]};
            hits += o.triangle[j] >= 0;
            var += double(o.variance[j]);
        }
        const double e = g.exposure / full;
        ptImage[i] = {acc.r * e, acc.g * e, acc.b * e};
        agree += hits == g.hits[i];                    // the same sub-samples covered on both sides
        if (g.hits[i] != full || hits != full) continue;
        ++both;
        const double yr = lum(raster[i]), yp = lum(ptImage[i]), dd = yr - yp;
        sr += yr; sp += yp; sd += dd; sd2 += dd * dd; sv += var * e * e;   // the variance of the block mean
        const int row = pixelGroup[i];
        if (row < 0 || std::size_t(row) >= G) continue;
        rowD2[std::size_t(row)] += dd * dd; rowP[std::size_t(row)] += yp; rowD[std::size_t(row)] += dd; ++rowN[std::size_t(row)];
    }
    const double n = both ? double(both) : 1.0, meanP = sp / n;
    // The split-sum diagnostic: image lighting alone, the raster's model against its reference.
    std::vector<double> ssD2(G), ssR(G); std::vector<long> ssN(G);
    for (std::size_t q = 0; q < g.samples.size(); q += std::size_t(std::max(1, d.splitStride))) {
        const lighting::Sample& sm = g.samples[q];
        if (sm.m.transmission > 0.0) continue;   // shadeIblReference integrates the upper hemisphere only
        if (q >= g.group.size() || std::size_t(g.group[q]) >= G) continue;
        const std::size_t row = std::size_t(g.group[q]);
        const double a = lum(lighting::shadeIbl(sm, t, prefiltered, g.sh)), ref = lum(lighting::shadeIblReference(sm, t, g.env, d.splitN));
        ssD2[row] += (a - ref) * (a - ref); ssR[row] += ref; ++ssN[row];
    }
    char b[8192];
    int at = std::snprintf(b, sizeof b,
        "{\n \"schema\": \"raw-native.pt-error/1\",\n \"scene\": \"%s\",\n \"width\": %d, \"height\": %d, \"raster_spp\": %d,\n"
        " \"spp\": %d, \"bounces\": %d, \"tessellation\": [%d, %d], \"env_scale\": %.6g,\n \"path_tracer_version\": %d,\n"
        " \"pixels_both_cover\": %ld,\n \"coverage_agreement\": %.6f,\n \"mean_luminance_raster\": %.6g, \"mean_luminance_pt\": %.6g,\n"
        " \"relative_rmse\": %.6g, \"bias\": %.6g, \"noise_floor\": %.6g,\n \"groups\": [",
        g.name.c_str(), W, H, full, d.spp, d.bounces, d.nu, d.nv, d.envScale, rt::kPathTraceVersion, both, double(agree) / double(W * H), sr / n, meanP,
        meanP > 0 ? std::sqrt(sd2 / n) / meanP : 0.0, meanP > 0 ? sd / n / meanP : 0.0, meanP > 0 ? std::sqrt(sv / n) / meanP : 0.0);
    for (std::size_t r = 0; r < G; ++r) {
        const std::string& label = g.groupLabels[r];
        const double rn = rowN[r] ? double(rowN[r]) : 1.0, rm = rowP[r] / rn;
        const double sn = ssN[r] ? double(ssN[r]) : 1.0, sm = ssR[r] / sn;
        at += std::snprintf(b + at, sizeof b - std::size_t(at), "%s\n  {\"group\": %zu, \"label\": \"%s\", \"pixels\": %ld, \"relative_rmse\": %.6g, \"bias\": %.6g, \"split_sum_relative_rmse\": %.6g, \"split_sum_samples\": %ld}",
                           r ? "," : "", r, label.c_str(), rowN[r], rm > 0 ? std::sqrt(rowD2[r] / rn) / rm : 0.0, rm > 0 ? rowD[r] / rn / rm : 0.0,
                           sm > 0 ? std::sqrt(ssD2[r] / sn) / sm : 0.0, ssN[r]);
    }
    std::snprintf(b + at, sizeof b - std::size_t(at), "\n ],\n \"pt_ms\": %.1f%s%s\n}\n", ms, o.error.empty() ? "" : ",\n \"error\": ", o.error.empty() ? "" : ("\"" + o.error + "\"").c_str());
    return b;
}

}  // namespace raw::gallery
