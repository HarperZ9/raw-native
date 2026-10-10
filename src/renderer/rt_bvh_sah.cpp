// The SAH cost of the M3 binned-SAH build: see raw/renderer/rt_bvh.hpp. raw::Bvh keeps its
// nodes private, so this file ports its split rule from src/renderer/bvh.cpp (12 bins, leaves
// of up to 4 triangles, the same stable partition) and sums the cost of the tree it makes.
// The port is checked against Bvh::nodeCount() in tests/test_rt_bvh.cpp.
#include "raw/renderer/rt_bvh.hpp"
#include <algorithm>
namespace raw::rt {
namespace {
constexpr int kBins = 12, kLeaf = 4;
float boxArea(const AABB& b) {
    const Vec3 d = b.mx - b.mn;
    return d.x < 0 ? 0.0f : 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}
AABB unite(AABB a, const AABB& b) { a.grow(b.mn); a.grow(b.mx); return a; }
float axis(Vec3 v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }
struct Port {
    const std::vector<Tri>& tris;
    std::vector<int> order;
    std::vector<Vec3> centres;
    double cost{0}, rootArea{0};
    std::size_t nodes{0};
    void split(int first, int count) {
        ++nodes;
        AABB box, cb;
        for (int i = first; i < first + count; ++i) {
            const Tri& t = tris[std::size_t(order[std::size_t(i)])];
            box.grow(t.a); box.grow(t.b); box.grow(t.c); cb.grow(centres[std::size_t(order[std::size_t(i)])]);
        }
        if (rootArea == 0.0) rootArea = boxArea(box);
        int bestAxis = -1, bestBin = 0;
        float bestCost = boxArea(box) * float(count);
        if (count > kLeaf)
            for (int k = 0; k < 3; ++k) {
                const float lo = axis(cb.mn, k), ext = axis(cb.mx, k) - lo;
                if (ext <= 0.0f) continue;
                AABB bb[kBins]; int cnt[kBins] = {};
                for (int i = first; i < first + count; ++i) {
                    const int o = order[std::size_t(i)], b = std::min(kBins - 1, int((axis(centres[std::size_t(o)], k) - lo) / ext * kBins));
                    const Tri& t = tris[std::size_t(o)];
                    bb[b].grow(t.a); bb[b].grow(t.b); bb[b].grow(t.c); ++cnt[b];
                }
                for (int s = 1; s < kBins; ++s) {
                    AABB l, r; int cl = 0, cr = 0;
                    for (int b = 0; b < s; ++b) if (cnt[b]) { l = unite(l, bb[b]); cl += cnt[b]; }
                    for (int b = s; b < kBins; ++b) if (cnt[b]) { r = unite(r, bb[b]); cr += cnt[b]; }
                    if (!cl || !cr) continue;
                    const float c = boxArea(l) * float(cl) + boxArea(r) * float(cr);
                    if (c < bestCost) { bestCost = c; bestAxis = k; bestBin = s; }
                }
            }
        if (bestAxis < 0) { cost += double(boxArea(box)) * double(count) / rootArea; return; }   // a leaf
        cost += double(boxArea(box)) / rootArea;                                                   // an internal node
        const float lo = axis(cb.mn, bestAxis), ext = axis(cb.mx, bestAxis) - lo;
        const auto mid = std::stable_partition(order.begin() + first, order.begin() + first + count, [&](int o) {
            return std::min(kBins - 1, int((axis(centres[std::size_t(o)], bestAxis) - lo) / ext * kBins)) < bestBin;
        });
        const int left = int(mid - (order.begin() + first));
        split(first, left);
        split(first + left, count - left);
    }
};
}  // namespace

double binnedSahCost(const std::vector<Tri>& tris, std::size_t* nodeCount) {
    Port p{tris, {}, {}};
    p.order.resize(tris.size());
    p.centres.resize(tris.size());
    for (std::size_t i = 0; i < tris.size(); ++i) { p.order[i] = int(i); p.centres[i] = (tris[i].a + tris[i].b + tris[i].c) * (1.0f / 3.0f); }
    if (!tris.empty()) p.split(0, int(tris.size()));
    if (nodeCount) *nodeCount = p.nodes;
    return p.cost;
}

}  // namespace raw::rt
