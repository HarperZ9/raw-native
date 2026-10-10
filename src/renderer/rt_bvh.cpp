// The PLOC BVH, CPU reference: see raw/renderer/rt_bvh.hpp.
#include "raw/renderer/rt_bvh.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <limits>
namespace raw::rt {
namespace {
void leafBox(const Tri& t, Node& n) {
    const Vec3 v[3] = {t.a, t.b, t.c};
    for (int k = 0; k < 3; ++k) {
        const float x[3] = {v[k].x, v[k].y, v[k].z};
        for (int a = 0; a < 3; ++a) {
            n.lo[a] = k ? std::min(n.lo[a], x[a]) : x[a];
            n.hi[a] = k ? std::max(n.hi[a], x[a]) : x[a];
        }
    }
}
float unionArea(const Node& a, const Node& b) {
    float lo[3], hi[3];
    for (int k = 0; k < 3; ++k) { lo[k] = std::min(a.lo[k], b.lo[k]); hi[k] = std::max(a.hi[k], b.hi[k]); }
    return area(lo, hi);
}
std::uint32_t spread(std::uint32_t v) {
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v << 8)) & 0x0300F00Fu;
    v = (v | (v << 4)) & 0x030C30C3u;
    v = (v | (v << 2)) & 0x09249249u;
    return v;
}
}  // namespace

Vec3 centroid(const Tri& t) {
    const float third = 0.333333343f;
    return {(t.a.x + t.b.x + t.c.x) * third, (t.a.y + t.b.y + t.c.y) * third, (t.a.z + t.b.z + t.c.z) * third};
}
float pow2Scale(float extent) {
    float s = 1.0f;
    if (!(extent > 0.0f)) return s;
    while (extent * s > 1.0f) s *= 0.5f;
    while (extent * s <= 0.5f) s *= 2.0f;
    return s;
}
std::uint32_t morton3(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (spread(x) << 2) | (spread(y) << 1) | spread(z); }
float area(const float* lo, const float* hi) {
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    // Written factored: SwiftShader's compiler rewrites dx dy + dy dz as dy (dx + dz), which
    // changed the tree on dense_block (evidence/rt-r2-runs.json, run 4). This form gives it no
    // common factor to pull out, so every backend evaluates the same operations.
    return 2.0f * (dy * (dx + dz) + dz * dx);
}

Tree buildPloc(const std::vector<Tri>& tris, int radius) {
    Tree T;
    const std::size_t n = tris.size();
    if (!n) return T;
    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    std::vector<Vec3> c(n);
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = centroid(tris[i]);
        const float x[3] = {c[i].x, c[i].y, c[i].z};
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], x[k]); hi[k] = std::max(hi[k], x[k]); }
    }
    const float s = pow2Scale(std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2])));
    std::vector<std::pair<std::uint32_t, std::uint32_t>> keys(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float x[3] = {c[i].x, c[i].y, c[i].z};
        std::uint32_t q[3];
        for (int k = 0; k < 3; ++k) q[k] = std::min(1023u, std::uint32_t((x[k] - lo[k]) * s * 1023.0f));
        keys[i] = {morton3(q[0], q[1], q[2]), std::uint32_t(i)};
    }
    std::sort(keys.begin(), keys.end());
    T.nodes.resize(2 * n - 1);
    std::vector<int> cl(n);
    for (std::size_t i = 0; i < n; ++i) {
        Node& L = T.nodes[i];
        L.tri = int(keys[i].second);
        leafBox(tris[keys[i].second], L);
        cl[i] = int(i);
    }
    int next = int(n);
    std::vector<int> nn;
    while (cl.size() > 1) {
        const int m = int(cl.size());
        nn.assign(std::size_t(m), -1);
        for (int i = 0; i < m; ++i) {
            float best = FLT_MAX;
            for (int j = std::max(0, i - radius); j <= std::min(m - 1, i + radius); ++j) {
                if (j == i) continue;
                const float d = unionArea(T.nodes[std::size_t(cl[std::size_t(i)])], T.nodes[std::size_t(cl[std::size_t(j)])]);
                if (d < best) { best = d; nn[std::size_t(i)] = j; }
            }
        }
        std::vector<int> out;
        out.reserve(std::size_t(m));
        for (int i = 0; i < m; ++i) {
            const int j = nn[std::size_t(i)];
            const bool mutual = nn[std::size_t(j)] == i;
            if (mutual && i > j) continue;
            if (!mutual) { out.push_back(cl[std::size_t(i)]); continue; }
            Node& N = T.nodes[std::size_t(next)];
            const Node& A = T.nodes[std::size_t(cl[std::size_t(i)])];
            const Node& B = T.nodes[std::size_t(cl[std::size_t(j)])];
            for (int k = 0; k < 3; ++k) { N.lo[k] = std::min(A.lo[k], B.lo[k]); N.hi[k] = std::max(A.hi[k], B.hi[k]); }
            N.left = cl[std::size_t(i)]; N.right = cl[std::size_t(j)];
            out.push_back(next++);
        }
        cl.swap(out);
        ++T.rounds;
    }
    T.root = cl[0];
    return T;
}

double sahCost(const Tree& t) {
    if (t.root < 0) return 0.0;
    const Node& r = t.nodes[std::size_t(t.root)];
    const double ra = area(r.lo, r.hi);
    double c = 0.0;
    for (const Node& n : t.nodes) c += double(area(n.lo, n.hi)) / ra;   // every node: one traversal step or one triangle test
    return c;
}

bool closest(const Tree& t, const std::vector<Tri>& tris, const Ray& r, float tMax, Hit& hit) {
    if (t.root < 0) return false;
    const auto inv = [](float x) { return x != 0.0f ? 1.0f / x : std::copysign(std::numeric_limits<float>::infinity(), x); };
    const float o[3] = {r.o.x, r.o.y, r.o.z}, iv[3] = {inv(r.d.x), inv(r.d.y), inv(r.d.z)};
    int stack[256], sp = 0;
    stack[sp++] = t.root;
    bool any = false;
    float best = tMax;
    while (sp) {
        const Node& n = t.nodes[std::size_t(stack[--sp])];
        float t0 = 0.0f, t1 = best;
        for (int k = 0; k < 3; ++k) {
            const float a = (n.lo[k] - o[k]) * iv[k], b = (n.hi[k] - o[k]) * iv[k];
            t0 = std::max(t0, std::min(a, b)); t1 = std::min(t1, std::max(a, b));
        }
        if (!(t0 <= t1 * kSlabExit)) continue;
        if (n.tri >= 0) {
            float tt, u, v;
            if (intersectTri(r, tris[std::size_t(n.tri)], tt, u, v) && (tt < best || (any && tt == best && n.tri < hit.tri))) {
                best = tt; hit = {tt, u, v, n.tri}; any = true;
            }
            continue;
        }
        if (sp > 253) std::abort();                               // deeper than 256: never silent
        stack[sp++] = n.right; stack[sp++] = n.left;
    }
    return any;
}

bool occluded(const Tree& t, const std::vector<Tri>& tris, const Ray& r, float tMax) {
    Hit h;
    return closest(t, tris, r, tMax, h);
}

}  // namespace raw::rt
