// Outlines and toon ramps on the CPU G-buffer: see raw/renderer/outline.hpp.
#include "raw/renderer/outline.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/math/mat.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw::outline {

std::vector<uint8_t> edges(const GBuffer& g, const Buffer<uint16_t>* ids, Edge kind, const Params& p) {
    const int W = g.w, H = g.h;
    std::vector<uint8_t> out(size_t(W) * H, 0);
    const auto covered = [&](int x, int y) { return g.mask.at(x, y) != 0; };
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        if (!covered(x, y)) continue;
        const float d = g.depth.at(x, y);
        for (int k = 0; k < 4; ++k) {
            const int qx = x + dx[k], qy = y + dy[k];
            if (qx < 0 || qy < 0 || qx >= W || qy >= H) continue;
            const bool qc = covered(qx, qy);
            bool edge = false;
            if (kind == Edge::Depth) {
                const float e = g.depth.at(qx, qy);
                edge = !qc || (e > d && (e - d) / d > p.depthRel);
            } else if (kind == Edge::Id) {
                const uint16_t a = ids->at(x, y), b = ids->at(qx, qy);
                edge = a != b && (b == 0 || g.depth.at(qx, qy) > d);
            } else {
                edge = qc && dot(g.normal.at(x, y), g.normal.at(qx, qy)) < p.normalCos;
            }
            if (edge) { out[size_t(y) * W + x] = 1; break; }
        }
    }
    return out;
}

std::vector<uint8_t> invertedHull(const Scene& scene, int w, int h, float thickness) {
    const GBuffer g = rasterize(scene, w, h);
    std::vector<float> hullDepth(size_t(w) * h, std::numeric_limits<float>::infinity());
    const Mat4 vp = mul(scene.camera.proj(), scene.camera.view());
    const Vec3 eye = scene.camera.eye;
    for (const Mesh& m : scene.meshes) {
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const int id[3] = {m.indices[i], m.indices[i + 1], m.indices[i + 2]};
            Vec3 wp[3], nsum{0, 0, 0}, c{0, 0, 0};
            for (int k = 0; k < 3; ++k) { wp[k] = m.positions[id[k]] + m.normals[id[k]] * thickness; nsum = nsum + m.normals[id[k]]; c = c + wp[k] * (1.0f / 3); }
            if (dot(nsum, eye - c) >= 0) continue;                 // keep the back faces only
            Vec3 sp[3]; bool behind = false;
            for (int k = 0; k < 3; ++k) {
                const Vec4 cs = mul(vp, Vec4{wp[k].x, wp[k].y, wp[k].z, 1});
                if (cs.w <= 1e-6f) { behind = true; break; }
                sp[k] = {(cs.x / cs.w * 0.5f + 0.5f) * w, (1.0f - (cs.y / cs.w * 0.5f + 0.5f)) * h, cs.w};
            }
            if (behind) continue;
            const float area = (sp[1].x - sp[0].x) * (sp[2].y - sp[0].y) - (sp[1].y - sp[0].y) * (sp[2].x - sp[0].x);
            if (std::fabs(area) < 1e-9f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({sp[0].x, sp[1].x, sp[2].x})))), x1 = std::min(w - 1, int(std::ceil(std::max({sp[0].x, sp[1].x, sp[2].x}))));
            const int y0 = std::max(0, int(std::floor(std::min({sp[0].y, sp[1].y, sp[2].y})))), y1 = std::min(h - 1, int(std::ceil(std::max({sp[0].y, sp[1].y, sp[2].y}))));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float a0 = ((sp[1].x - px) * (sp[2].y - py) - (sp[1].y - py) * (sp[2].x - px)) / area;
                const float a1 = ((sp[2].x - px) * (sp[0].y - py) - (sp[2].y - py) * (sp[0].x - px)) / area, a2 = 1 - a0 - a1;
                if (a0 < 0 || a1 < 0 || a2 < 0) continue;
                float& hd = hullDepth[size_t(y) * w + x];
                hd = std::min(hd, a0 * sp[0].z + a1 * sp[1].z + a2 * sp[2].z);
            }
        }
    }
    // The mesh is drawn over its hull with a depth test: the hull shows where the mesh
    // does not cover the pixel or lies behind it.
    std::vector<uint8_t> out(size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const float hd = hullDepth[size_t(y) * w + x];
        if (hd < std::numeric_limits<float>::infinity() && !(g.mask.at(x, y) && g.depth.at(x, y) <= hd)) out[size_t(y) * w + x] = 1;
    }
    return out;
}

std::vector<int8_t> toonBands(const GBuffer& g, Vec3 lightDir, int bands) {
    std::vector<int8_t> out(size_t(g.w) * g.h, -1);
    const Vec3 l = normalize(lightDir) * -1.0f;                   // toward the light
    for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x) {
        if (!g.mask.at(x, y)) continue;
        const float nl = std::max(0.0f, dot(normalize(g.normal.at(x, y)), l));
        out[size_t(y) * g.w + x] = int8_t(std::min(bands - 1, int(std::floor(nl * bands))));
    }
    return out;
}

}  // namespace raw::outline
