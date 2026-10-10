// GPU parity of the cascaded shadow maps: see raw/renderer/shadow_parity.hpp.
#include "shadow_parity_internal.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/raster_identity.hpp"
#include "frame_pack.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace raw::gpu_check {
namespace {
using shadows::D3;
struct V2 { double x, y; };

// Triangle t's corners in cascade texels, as rasterizeCascade computes and snaps them.
void texelTri(const std::vector<float>& tris, std::size_t t, const std::array<float, 16>& M, int size, int bits, V2 out[3]) {
    const float q = bits > 0 ? float(1 << bits) : 0.0f, S = float(size);
    for (int k = 0; k < 3; ++k) {
        const float* p = &tris[t * 24 + std::size_t(k) * 3];
        const float x = M[0] * p[0] + M[1] * p[1] + M[2] * p[2] + M[3], y = M[4] * p[0] + M[5] * p[1] + M[6] * p[2] + M[7];
        float sx = (x * 0.5f + 0.5f) * S, sy = (1.0f - (y * 0.5f + 0.5f)) * S;
        if (q > 0.0f) { sx = std::nearbyint(sx * q) / q; sy = std::nearbyint(sy * q) / q; }
        out[k] = {sx, sy};
    }
}
double edgeDistance(const V2 v[3], double px, double py) {
    double best = 1e30;
    for (int k = 0; k < 3; ++k) {
        const V2 a = v[k], b = v[(k + 1) % 3];
        const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        const double t = l2 > 0 ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, std::hypot(px - (a.x + t * dx), py - (a.y + t * dy)));
    }
    return best;
}
ShadowMapCase compareMap(int k, const shadows::ShadowMap& g, const shadows::ShadowMap& c, const std::vector<float>& gpuTris,
                         const std::vector<float>& cpuTris, const std::array<float, 16>& M, int bits) {
    ShadowMapCase r; r.cascade = k;
    for (int y = 0; y < g.size; ++y) for (int x = 0; x < g.size; ++x) {
        const std::size_t at = std::size_t(y) * std::size_t(g.size) + std::size_t(x);
        const uint32_t gt = g.tri[at], ct = c.tri[at];
        if (!gt && !ct) continue;
        if (gt == ct) {
            ++r.both;
            const double dd = std::fabs(double(g.depth[at]) - c.depth[at]);
            r.worstDepth = std::max(r.worstDepth, dd);
            r.depthOver += dd > 1e-5;
            if (dd > 1e-5 && std::getenv("RAW_NATIVE_SHADOW_DUMP")) {   // diagnosis: the plane in double at the texel centre
                V2 v[3]; texelTri(cpuTris, gt - 1, M, g.size, bits, v);
                double z[3];
                for (int q = 0; q < 3; ++q) { const float* p = &cpuTris[(gt - 1) * 24 + std::size_t(q) * 3]; z[q] = double(M[8]) * p[0] + double(M[9]) * p[1] + double(M[10]) * p[2] + M[11]; }
                const double area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x), px = x + 0.5, py = y + 0.5;
                const double w0 = ((v[1].x - px) * (v[2].y - py) - (v[1].y - py) * (v[2].x - px)) / area, w1 = ((v[2].x - px) * (v[0].y - py) - (v[2].y - py) * (v[0].x - px)) / area;
                std::fprintf(stderr, "cascade %d texel %d %d tri %u gpu %.8f cpu %.8f double %.8f area %.4f z %.6f %.6f %.6f\n", k, x, y, gt, g.depth[at], c.depth[at],
                             w0 * z[0] + w1 * z[1] + (1 - w0 - w1) * z[2], area, z[0], z[1], z[2]);
            }
            continue;
        }
        ++r.coverageDiffs;
        double nearest = 1e30;
        for (uint32_t t : {gt, ct}) {
            if (!t) continue;
            V2 v[3];
            texelTri(t == gt ? gpuTris : cpuTris, t - 1, M, g.size, bits, v);
            nearest = std::min(nearest, edgeDistance(v, x + 0.5, y + 0.5));
        }
        // The bound exempts edges only. Depth ties (two surfaces, or a surface and the cleared far
        // value, within 1e-5) are counted for the record and stay failures.
        const double ga = gt ? double(g.depth[at]) : 1.0, ca = ct ? double(c.depth[at]) : 1.0;
        if (std::fabs(ga - ca) <= 1e-5) ++r.depthTies;
        if (nearest <= 0.01) ++r.explained;
        else if (std::getenv("RAW_NATIVE_SHADOW_DUMP"))   // diagnosis
            std::fprintf(stderr, "cascade %d texel %d %d gpu tri %u depth %.8f cpu tri %u depth %.8f edge %.4f\n", k, x, y, gt, g.depth[at], ct, c.depth[at], nearest);
    }
    return r;
}
// G-buffer points as shadow.wgsl reads them, and the view depth per pixel (negative where empty).
void packPoints(const GBuffer& g, std::vector<float>& pts, std::vector<float>& dist) {
    const int w = g.depth.w, h = g.depth.h;
    pts.assign(std::size_t(w) * h * 8, 0.0f);
    dist.assign(std::size_t(w) * h, -1.0f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!g.mask.at(x, y)) continue;
        const std::size_t i = std::size_t(y) * w + x;
        const Vec3 p = g.position.at(x, y), n = g.normal.at(x, y);
        const float v[8] = {p.x, p.y, p.z, g.depth.at(x, y), n.x, n.y, n.z, 1.0f};
        std::copy(v, v + 8, pts.begin() + std::ptrdiff_t(i * 8));
        dist[i] = g.depth.at(x, y);
    }
}
// The CPU lookups of every point on `maps`: hard, PCF, PCSS.
std::vector<double> cpuLookups(const shadows::CascadeSet& cs, const std::array<shadow_gpu::GpuMap, shadows::kCascades>& m, const std::vector<float>& pts) {
    const std::array<const shadows::ShadowMap*, shadows::kCascades> ptr{&m[0].map, &m[1].map, &m[2].map, &m[3].map};
    std::vector<double> out(pts.size() / 8 * 3, 1.0);
    for (std::size_t i = 0; i < pts.size() / 8; ++i) {
        const float* q = &pts[i * 8];
        if (q[7] == 0.0f) continue;
        for (int f = 0; f < 3; ++f)
            out[i * 3 + std::size_t(f)] = shadows::lookup(cs, ptr, {q[0], q[1], q[2]}, {q[4], q[5], q[6]}, q[3], shadows::Filter(f));
    }
    return out;
}
std::vector<float> cpuContact(const GBuffer& g, const Scene& s, D3 lightDir) {
    Buffer<float> dist; dist.resize(g.depth.w, g.depth.h);
    for (int y = 0; y < g.depth.h; ++y) for (int x = 0; x < g.depth.w; ++x)
        dist.at(x, y) = g.mask.at(x, y) ? g.depth.at(x, y) : std::numeric_limits<float>::infinity();
    const Buffer<float> v = shadows::contactShadows(dist, g.position, s.camera, lightDir, int(shadow_gpu::kContactSteps),
                                                    shadow_gpu::kContactLength, shadow_gpu::kContactThickness);
    return std::vector<float>(v.px.begin(), v.px.end());
}
// The scene with its last mesh moved 1% of the scene's extent along `dir` (the controls).
Scene moved(const Scene& s, D3 dir) {
    Scene m = s;
    float lo = 1e30f, hi = -1e30f;
    for (const Mesh& me : s.meshes) for (const Vec3& p : me.positions) { lo = std::min({lo, p.x, p.y, p.z}); hi = std::max({hi, p.x, p.y, p.z}); }
    const float d = 0.01f * (hi - lo);
    for (Vec3& p : m.meshes.back().positions) p = p + Vec3{float(dir.x) * d, float(dir.y) * d, float(dir.z) * d};
    return m;
}
}  // namespace

bool ShadowSceneCase::pass() const {
    bool ok = points > 0 && contactPixels > 0 && !maps.empty() && double(contactDiffs) <= 1e-3 * double(contactPixels);
    long covered = 0;
    for (const ShadowMapCase& m : maps) { ok = ok && m.pass(); covered += m.both; }
    ok = ok && covered > 0;
    for (long o : outside) ok = ok && double(o) <= 1e-3 * double(points);
    return ok;
}
bool ShadowParity::pass() const {
    bool ok = error.empty() && !scenes.empty() && mapControlFails && lookupControlFails && contactControlFails;
    for (const ShadowSceneCase& s : scenes) ok = ok && s.pass();
    return ok;
}

ShadowParity shadowParity(rhi::Device& dev, const std::vector<std::pair<std::string, const Scene*>>& scenes, int size, int mapSize) {
    ShadowParity r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    r.subpixelBits = probeSubpixelBits(dev, r.error);
    if (!r.error.empty()) return r;
    for (std::size_t si = 0; si < scenes.size(); ++si) {
        const Scene& s = *scenes[si].second;
        const D3 L{s.lights[0].dir.x, s.lights[0].dir.y, s.lights[0].dir.z};
        const double ll = std::sqrt(L.x * L.x + L.y * L.y + L.z * L.z);
        const D3 toL{-L.x / ll, -L.y / ll, -L.z / ll};
        const shadows::CascadeSet cs = shadows::fitCascades(s.camera, L, 40.0, mapSize, true);
        const std::vector<float> tris = gpu_host::packTriangles(s);
        std::array<shadow_gpu::GpuMap, shadows::kCascades> gm;
        if (!shadow_gpu::maps(dev, tris, cs, gm, r.error)) return r;
        ShadowSceneCase c; c.scene = scenes[si].first;
        std::array<shadows::ShadowMap, shadows::kCascades> cm;
        for (int k = 0; k < shadows::kCascades; ++k) {
            cm[std::size_t(k)] = shadows::rasterizeCascade(s, cs, k, r.subpixelBits);
            c.maps.push_back(compareMap(k, gm[std::size_t(k)].map, cm[std::size_t(k)], tris, tris, cs.matrix(k), r.subpixelBits));
        }
        RasterOptions ro; ro.perspectiveDepth = true;
        const GBuffer g = rasterize(s, size, size, nullptr, nullptr, ro);
        std::vector<float> pts, dist, gl, gc;
        packPoints(g, pts, dist);
        if (!shadow_gpu::lookups(dev, cs, gm, pts, gl, r.error)) return r;
        const std::vector<double> cl = cpuLookups(cs, gm, pts);
        for (std::size_t i = 0; i < pts.size() / 8; ++i) {
            const int kc = pts[i * 8 + 7] == 0.0f ? -1 : shadows::cascadeOf(cs, pts[i * 8 + 3]);
            if (kc < 0) continue;
            ++c.points; ++c.perCascade[kc];
            for (int f = 0; f < 3; ++f) {
                const double e = std::fabs(double(gl[i * 3 + std::size_t(f)]) - cl[i * 3 + std::size_t(f)]);
                c.worst[f] = std::max(c.worst[f], e);
                if (e > 1e-3) ++c.outside[f];
            }
        }
        const Mat4 vp = mul(s.camera.proj(), s.camera.view());
        if (!shadow_gpu::contact(dev, cs, dist, pts, size, size, vp, toL, gc, r.error)) return r;
        const std::vector<float> cc = cpuContact(g, s, L);
        for (std::size_t i = 0; i < dist.size(); ++i) if (dist[i] >= 0.0f) { ++c.contactPixels; c.contactDiffs += gc[i] != cc[i]; }
        if (si == 0) {   // the controls: the last mesh moved on the GPU side only
            const Scene ms = moved(s, cs.right);
            const std::vector<float> mt = gpu_host::packTriangles(ms);
            std::array<shadow_gpu::GpuMap, shadows::kCascades> mm;
            if (!shadow_gpu::maps(dev, mt, cs, mm, r.error)) return r;
            for (int k = 0; k < shadows::kCascades; ++k) {
                const ShadowMapCase q = compareMap(k, mm[std::size_t(k)].map, cm[std::size_t(k)], mt, tris, cs.matrix(k), r.subpixelBits);
                r.mapControlFails = r.mapControlFails || !q.pass();
                r.mapControlUnexplained += q.coverageDiffs - q.explained;
            }
            std::vector<float> ml, mc, mpts, mdist;
            if (!shadow_gpu::lookups(dev, cs, mm, pts, ml, r.error)) return r;
            long off = 0;
            for (std::size_t i = 0; i < cl.size(); ++i) off += std::fabs(double(ml[i]) - cl[i]) > 1e-3;
            r.lookupControlFails = double(off) > 1e-3 * double(c.points); r.lookupControlOff = off;
            packPoints(rasterize(ms, size, size, nullptr, nullptr, ro), mpts, mdist);
            if (!shadow_gpu::contact(dev, cs, mdist, mpts, size, size, vp, toL, mc, r.error)) return r;
            long cd = 0;
            for (std::size_t i = 0; i < mc.size(); ++i) cd += mc[i] != cc[i];
            r.contactControlFails = double(cd) > 1e-3 * double(c.contactPixels); r.contactControlDiffs = cd;
        }
        r.scenes.push_back(std::move(c));
    }
    return r;
}

std::string ShadowParity::json() const {
    std::string e;
    for (char ch : error) { if (ch == '"' || ch == '\\') e += '\\'; e += ch == '\n' ? ' ' : ch; }
    std::string s = "{\n \"check\": \"m3 shadows gpu parity\",\n \"backend\": \"" + backend + "\",\n \"adapter\": \"" + adapter + "\",\n \"error\": \"" + e +
                    "\",\n \"subpixel_bits\": " + std::to_string(subpixelBits) + ",\n \"scenes\": [\n";
    for (std::size_t k = 0; k < scenes.size(); ++k) {
        const ShadowSceneCase& c = scenes[k];
        std::string m;
        for (const ShadowMapCase& q : c.maps) {
            char b[256];
            std::snprintf(b, sizeof b, "%s{\"cascade\": %d, \"same_triangle_texels\": %ld, \"differing_texels\": %ld, \"explained\": %ld, \"worst_depth\": %.3e, \"depth_over_1e-5\": %ld, \"depth_ties\": %ld}",
                          m.empty() ? "" : ", ", q.cascade, q.both, q.coverageDiffs, q.explained, q.worstDepth, q.depthOver, q.depthTies);
            m += b;
        }
        char b[2048];
        std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"maps\": [%s], \"points\": %ld, \"per_cascade\": [%ld, %ld, %ld, %ld], \"outside_1e-3\": {\"hard\": %ld, \"pcf\": %ld, \"pcss\": %ld},"
                      " \"worst\": {\"hard\": %.3e, \"pcf\": %.3e, \"pcss\": %.3e}, \"contact_pixels\": %ld, \"contact_diffs\": %ld, \"pass\": %s}%s\n",
                      c.scene.c_str(), m.c_str(), c.points, c.perCascade[0], c.perCascade[1], c.perCascade[2], c.perCascade[3], c.outside[0], c.outside[1], c.outside[2], c.worst[0], c.worst[1], c.worst[2], c.contactPixels,
                      c.contactDiffs, c.pass() ? "true" : "false", k + 1 < scenes.size() ? "," : "");
        s += b;
    }
    return s + " ],\n \"controls_fail\": {\"map\": " + (mapControlFails ? "true" : "false") + ", \"lookup\": " + (lookupControlFails ? "true" : "false") +
           ", \"contact\": " + (contactControlFails ? "true" : "false") + "},\n \"controls_changed\": {\"map_unexplained_texels\": " +
           std::to_string(mapControlUnexplained) + ", \"lookup_values\": " + std::to_string(lookupControlOff) + ", \"contact_pixels\": " +
           std::to_string(contactControlDiffs) + "},\n \"pass\": " + (pass() ? "true" : "false") + "\n}\n";
}
}  // namespace raw::gpu_check
