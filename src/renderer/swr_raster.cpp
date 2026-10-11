// The CPU reference rasterizer and resolve: see raw/renderer/swr.hpp. Slots are drawn in
// index order and the depth test is strictly less, so equal depths keep the lower slot;
// the GPU's per-tile lists are in the same order and reach the same answer.
#include "raw/renderer/swr.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw::swr {

std::uint32_t TextureSet::add(std::uint32_t w, std::uint32_t h, const std::vector<std::uint32_t>& px) {
    entries.push_back({std::uint32_t(texels.size()), w, h});
    texels.insert(texels.end(), px.begin(), px.end());
    return std::uint32_t(entries.size() - 1);
}

namespace {
struct Weights { bool in; float w0, w1, w2; };
Weights weightsAt(const std::int32_t* I, const float* F, std::int32_t px, std::int32_t py, FillRule rule) {
    const std::int64_t e12 = edge(I[2], I[3], I[4], I[5], px, py);
    const std::int64_t e20 = edge(I[4], I[5], I[0], I[1], px, py);
    const std::int64_t e01 = edge(I[0], I[1], I[2], I[3], px, py);
    const bool in = covers(e12, topLeft(I[4] - I[2], I[5] - I[3]), rule) &&
                    covers(e20, topLeft(I[0] - I[4], I[1] - I[5]), rule) &&
                    covers(e01, topLeft(I[2] - I[0], I[3] - I[1]), rule);
    if (!in) return {false, 0, 0, 0};
    return {true, edgeToFloat(e12) * F[6], edgeToFloat(e20) * F[6], edgeToFloat(e01) * F[6]};
}
}  // namespace

Visibility rasterize(const Setup& s, const Options& o) {
    Visibility v;
    v.width = s.width; v.height = s.height;
    const std::size_t n = std::size_t(s.width) * std::size_t(s.height);
    v.slot.assign(n, 0u);
    v.depth.assign(n, std::numeric_limits<float>::infinity());
    v.depthQ.assign(n, 0xffffffffu);
    v.count.assign(n, 0u);
    const float maxQ = o.depthBits > 0 ? float((1u << o.depthBits) - 1u) : 0.0f;
    for (std::size_t slot = 0; slot < s.slots(); ++slot) {
        const std::int32_t* I = &s.ints[slot * kSetupInts];
        const float* F = &s.floats[slot * kSetupFloats];
        if (I[6] == 0) continue;
        for (std::int32_t y = I[8]; y <= I[10]; ++y)
            for (std::int32_t x = I[7]; x <= I[9]; ++x) {
                const Weights w = weightsAt(I, F, x * 256 + 128, y * 256 + 128, o.rule);
                if (!w.in) continue;
                const std::size_t p = std::size_t(y) * std::size_t(s.width) + std::size_t(x);
                ++v.count[p];
                const float depth = F[0] + w.w1 * (F[1] - F[0]) + w.w2 * (F[2] - F[0]);
                std::uint32_t q = 0;
                if (o.depthBits > 0) {
                    const float d01 = std::clamp(depth * 0.5f + 0.5f, 0.0f, 1.0f);
                    q = std::uint32_t(std::floor(d01 * maxQ + float(bayer4(x, y)) * 0.0625f));
                }
                if (o.depthTest) {
                    if (o.depthBits > 0 ? !(q < v.depthQ[p]) : !(depth < v.depth[p])) continue;
                }
                v.slot[p] = std::uint32_t(slot + 1);
                v.depth[p] = depth;
                v.depthQ[p] = q;
            }
    }
    return v;
}

Resolved resolve(const Setup& s, const Visibility& v, const Geometry& g, const TextureSet& t, Vec3 light, const Options& o) {
    Resolved r;
    const std::size_t n = std::size_t(s.width) * std::size_t(s.height);
    r.uv.assign(n * 2, 0.0f); r.texelCoord.assign(n * 2, 0.0f);
    r.texel.assign(n, 0xffffffffu); r.colour.assign(n, 0u); r.normal.assign(n * 3, 0.0f);
    const float len = std::sqrt(light.x * light.x + light.y * light.y + light.z * light.z);
    const Vec3 L{light.x / len, light.y / len, light.z / len};
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            const std::size_t p = std::size_t(y) * std::size_t(s.width) + std::size_t(x);
            if (!v.slot[p]) continue;
            const std::size_t slot = v.slot[p] - 1;
            const std::int32_t* I = &s.ints[slot * kSetupInts];
            const float* F = &s.floats[slot * kSetupFloats];
            const Weights w = weightsAt(I, F, x * 256 + 128, y * 256 + 128, FillRule::Inclusive);
            float l0, l1, l2;
            if (o.affine) {
                const float sum = w.w0 + w.w1 + w.w2;
                l0 = w.w0 / sum; l1 = w.w1 / sum; l2 = w.w2 / sum;
            } else {
                const float q0 = w.w0 * F[3], q1 = w.w1 * F[4], q2 = w.w2 * F[5];
                const float den = q0 + q1 + q2;
                l0 = q0 / den; l1 = q1 / den; l2 = q2 / den;
            }
            const float b1 = l0 * F[7] + l1 * F[9] + l2 * F[11];
            const float b2 = l0 * F[8] + l1 * F[10] + l2 * F[12];
            const float b0 = (1.0f - b1) - b2;
            const std::size_t tri = std::size_t(I[6] - 1);
            const std::uint32_t i0 = g.idx[tri * 3], i1 = g.idx[tri * 3 + 1], i2 = g.idx[tri * 3 + 2];
            const float u = g.uv[i0].x * b0 + g.uv[i1].x * b1 + g.uv[i2].x * b2;
            const float vv = g.uv[i0].y * b0 + g.uv[i1].y * b1 + g.uv[i2].y * b2;
            const float nx = g.nrm[i0].x * b0 + g.nrm[i1].x * b1 + g.nrm[i2].x * b2;
            const float ny = g.nrm[i0].y * b0 + g.nrm[i1].y * b1 + g.nrm[i2].y * b2;
            const float nz = g.nrm[i0].z * b0 + g.nrm[i1].z * b1 + g.nrm[i2].z * b2;
            const float nl = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            const float shade = 0.35f + 0.65f * std::max(0.0f, (nx * L.x + ny * L.y + nz * L.z) * nl);
            const std::uint32_t texId = g.triTexture[tri];
            const TextureSet::Entry& e = t.entries[texId];
            const float tu = u * float(e.width), tv = vv * float(e.height);
            const std::uint32_t tx = std::uint32_t(std::int32_t(std::floor(tu))) & (e.width - 1);
            const std::uint32_t ty = std::uint32_t(std::int32_t(std::floor(tv))) & (e.height - 1);
            const std::uint32_t c = t.texels[e.offset + ty * e.width + tx];
            std::uint32_t out = 0xff000000u;
            for (int k = 0; k < 3; ++k) {
                const float ch = float((c >> (8 * k)) & 255u) * shade;
                out |= std::min(255u, std::uint32_t(std::floor(ch + 0.5f))) << (8 * k);
            }
            r.uv[p * 2] = u; r.uv[p * 2 + 1] = vv;
            r.texelCoord[p * 2] = tu; r.texelCoord[p * 2 + 1] = tv;
            r.texel[p] = tx | (ty << 12) | (texId << 24);
            r.colour[p] = out;
            r.normal[p * 3] = nx * nl; r.normal[p * 3 + 1] = ny * nl; r.normal[p * 3 + 2] = nz * nl;
        }
    return r;
}

}  // namespace raw::swr
