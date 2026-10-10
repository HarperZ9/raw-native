// The triangle BVH: see raw/renderer/bvh.hpp.
#include "raw/renderer/bvh.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
namespace raw {
namespace {
constexpr int kBins = 12, kLeaf = 4;
float area(const AABB& b) {
    const Vec3 d = b.mx - b.mn;
    return d.x < 0 ? 0.0f : 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}
AABB unite(AABB a, const AABB& b) { a.grow(b.mn); a.grow(b.mx); return a; }
float axis(Vec3 v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }
// Slab test: the entry distance, or +inf when the ray misses the box before tMax.
float entry(const AABB& b, Vec3 o, Vec3 inv, float tMax) {
    float t0 = 0.0f, t1 = tMax;
    for (int k = 0; k < 3; ++k) {
        const float i = axis(inv, k), a = (axis(b.mn, k) - axis(o, k)) * i, c = (axis(b.mx, k) - axis(o, k)) * i;
        t0 = std::max(t0, std::min(a, c)); t1 = std::min(t1, std::max(a, c));
    }
    return t0 <= t1 ? t0 : std::numeric_limits<float>::infinity();
}
Vec3 inverse(Vec3 d) {
    const auto f = [](float x) { return x != 0.0f ? 1.0f / x : std::copysign(std::numeric_limits<float>::infinity(), x); };
    return {f(d.x), f(d.y), f(d.z)};
}
}  // namespace

void Bvh::build(std::vector<Tri> tris) {
    tris_ = std::move(tris);
    nodes_.clear();
    std::vector<int> order(tris_.size());
    std::vector<Vec3> centres(tris_.size());
    for (std::size_t i = 0; i < tris_.size(); ++i) { order[i] = int(i); centres[i] = (tris_[i].a + tris_[i].b + tris_[i].c) * (1.0f / 3.0f); }
    nodes_.push_back({{}, 0, int(tris_.size()), 0});
    if (!tris_.empty()) split(0, order, centres);
    std::vector<Tri> sorted(tris_.size());
    for (std::size_t i = 0; i < order.size(); ++i) sorted[i] = tris_[std::size_t(order[i])];
    tris_.swap(sorted);
    index_.assign(order.begin(), order.end());
}

void Bvh::split(int ni, std::vector<int>& order, std::vector<Vec3>& centres) {
    Node& n = nodes_[std::size_t(ni)];
    AABB box, cb;
    for (int i = n.first; i < n.first + n.count; ++i) {
        const Tri& t = tris_[std::size_t(order[std::size_t(i)])];
        box.grow(t.a); box.grow(t.b); box.grow(t.c); cb.grow(centres[std::size_t(order[std::size_t(i)])]);
    }
    n.box = box;
    if (n.count <= kLeaf) return;
    int bestAxis = -1, bestBin = 0;
    float bestCost = area(box) * float(n.count);
    for (int k = 0; k < 3; ++k) {
        const float lo = axis(cb.mn, k), ext = axis(cb.mx, k) - lo;
        if (ext <= 0.0f) continue;
        AABB bb[kBins]; int cnt[kBins] = {};
        for (int i = n.first; i < n.first + n.count; ++i) {
            const int o = order[std::size_t(i)], b = std::min(kBins - 1, int((axis(centres[std::size_t(o)], k) - lo) / ext * kBins));
            const Tri& t = tris_[std::size_t(o)];
            bb[b].grow(t.a); bb[b].grow(t.b); bb[b].grow(t.c); ++cnt[b];
        }
        for (int s = 1; s < kBins; ++s) {
            AABB l, r; int cl = 0, cr = 0;
            for (int b = 0; b < s; ++b) if (cnt[b]) { l = unite(l, bb[b]); cl += cnt[b]; }
            for (int b = s; b < kBins; ++b) if (cnt[b]) { r = unite(r, bb[b]); cr += cnt[b]; }
            if (!cl || !cr) continue;
            const float cost = area(l) * float(cl) + area(r) * float(cr);
            if (cost < bestCost) { bestCost = cost; bestAxis = k; bestBin = s; }
        }
    }
    if (bestAxis < 0) return;                                   // no split beats a leaf
    const float lo = axis(cb.mn, bestAxis), ext = axis(cb.mx, bestAxis) - lo;
    const auto mid = std::stable_partition(order.begin() + n.first, order.begin() + n.first + n.count, [&](int o) {
        return std::min(kBins - 1, int((axis(centres[std::size_t(o)], bestAxis) - lo) / ext * kBins)) < bestBin;
    });
    const int first = n.first, count = n.count, leftCount = int(mid - (order.begin() + first));
    const int left = int(nodes_.size());
    nodes_.push_back({{}, first, leftCount, 0});
    nodes_.push_back({{}, first + leftCount, count - leftCount, 0});
    nodes_[std::size_t(ni)].count = 0;
    nodes_[std::size_t(ni)].first = left;
    nodes_[std::size_t(ni)].right = left + 1;
    split(left, order, centres);
    split(left + 1, order, centres);
}

bool Bvh::closest(const Ray& r, float tMax, Hit& hit) const {
    if (nodes_.empty() || tris_.empty()) return false;
    const Vec3 inv = inverse(r.d);
    int stack[128], sp = 0;
    stack[sp++] = 0;
    bool any = false;
    float best = tMax;
    while (sp) {
        const Node& n = nodes_[std::size_t(stack[--sp])];
        if (entry(n.box, r.o, inv, best) == std::numeric_limits<float>::infinity()) continue;
        if (n.count) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                float t, u, v;
                if (!intersectTri(r, tris_[std::size_t(i)], t, u, v)) continue;
                const int id = index_[std::size_t(i)];
                if (t < best || (any && t == best && id < hit.tri)) { best = t; hit = {t, u, v, id}; any = true; }
            }
        } else {
            if (sp > 125) std::abort();                         // deeper than any SAH tree of 2^31 triangles; never silent
            stack[sp++] = n.right; stack[sp++] = n.first;
        }
    }
    return any;
}

bool Bvh::occluded(const Ray& r, float tMax) const {
    if (nodes_.empty() || tris_.empty()) return false;
    const Vec3 inv = inverse(r.d);
    int stack[128], sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node& n = nodes_[std::size_t(stack[--sp])];
        if (entry(n.box, r.o, inv, tMax) == std::numeric_limits<float>::infinity()) continue;
        if (n.count) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                float t, u, v;
                if (intersectTri(r, tris_[std::size_t(i)], t, u, v) && t < tMax) return true;
            }
        } else {
            if (sp > 125) std::abort();                         // deeper than any SAH tree of 2^31 triangles; never silent
            stack[sp++] = n.right; stack[sp++] = n.first;
        }
    }
    return false;
}

}  // namespace raw
