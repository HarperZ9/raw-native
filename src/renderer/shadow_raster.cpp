// The CPU shadow-map rasterizer and contact shadows: see raw/renderer/shadows.hpp.
#include "raw/renderer/shadows.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw::shadows {

// Light-space clip is orthographic (w = 1), so depth is linear in screen space. Vertices
// snap to 1/2^bits px as the hardware does, and coverage is the inclusive edge test of
// src/renderer/raster.cpp; depth outside [0, 1] is clipped, as the hardware clips.
ShadowMap rasterizeCascade(const Scene& scene, const CascadeSet& cs, int k, int bits) {
    ShadowMap m;
    m.size = cs.size;
    m.depth.assign(std::size_t(m.size) * m.size, 1.0f);
    m.tri.assign(m.depth.size(), 0u);
    const std::array<float, 16> M = cs.matrix(k);
    const float q = bits > 0 ? float(1 << bits) : 0.0f, S = float(m.size);
    uint32_t index = 0;
    for (const Mesh& mesh : scene.meshes) {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3, ++index) {
            float sx[3], sy[3], sz[3];
            for (int c = 0; c < 3; ++c) {
                const Vec3 p = mesh.positions[std::size_t(mesh.indices[i + std::size_t(c)])];
                const float x = M[0] * p.x + M[1] * p.y + M[2] * p.z + M[3];
                const float y = M[4] * p.x + M[5] * p.y + M[6] * p.z + M[7];
                sz[c] = M[8] * p.x + M[9] * p.y + M[10] * p.z + M[11];
                sx[c] = (x * 0.5f + 0.5f) * S; sy[c] = (1.0f - (y * 0.5f + 0.5f)) * S;
                if (q > 0.0f) { sx[c] = std::nearbyint(sx[c] * q) / q; sy[c] = std::nearbyint(sy[c] * q) / q; }
            }
            const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
            if (std::fabs(area) < 1e-12f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]})))), x1 = std::min(m.size - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
            const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]})))), y1 = std::min(m.size - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float w0 = ((sx[1] - px) * (sy[2] - py) - (sy[1] - py) * (sx[2] - px)) / area;
                const float w1 = ((sx[2] - px) * (sy[0] - py) - (sy[2] - py) * (sx[0] - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                if (!(z >= 0.0f && z <= 1.0f)) continue;
                const std::size_t at = std::size_t(y) * m.size + x;
                if (z < m.depth[at]) { m.depth[at] = z; m.tri[at] = index + 1; }
            }
        }
    }
    return m;
}

// March from each pixel's surface toward the light in view space; a step that lands behind
// the depth buffer's surface by less than `thickness` is an occluder.
Buffer<float> contactShadows(const Buffer<float>& dist, const Buffer<Vec3>& position, const Camera& cam, D3 lightDir, int steps,
                             double length, double thickness) {
    Buffer<float> out; out.resize(dist.w, dist.h);
    const Mat4 vp = mul(cam.proj(), cam.view());
    const double ll = std::sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
    const D3 toLight{-lightDir.x / ll, -lightDir.y / ll, -lightDir.z / ll};
    for (int y = 0; y < dist.h; ++y) for (int x = 0; x < dist.w; ++x) {
        float v = 1.0f;
        if (std::isfinite(dist.at(x, y))) {
            const Vec3 p = position.at(x, y);
            for (int i = 1; i <= steps && v > 0.0f; ++i) {
                const double t = length * i / steps;
                const Vec4 c = mul(vp, Vec4{float(p.x + toLight.x * t), float(p.y + toLight.y * t), float(p.z + toLight.z * t), 1.0f});
                if (c.w <= 1e-6f) break;
                const int sx = int(std::floor((c.x / c.w * 0.5f + 0.5f) * dist.w)), sy = int(std::floor((1.0f - (c.y / c.w * 0.5f + 0.5f)) * dist.h));
                if (sx < 0 || sy < 0 || sx >= dist.w || sy >= dist.h) break;
                const float d = dist.at(sx, sy);
                if (std::isfinite(d) && c.w > d + 1e-3f && c.w - d < float(thickness)) v = 0.0f;
            }
        }
        out.at(x, y) = v;
    }
    return out;
}

}  // namespace raw::shadows
