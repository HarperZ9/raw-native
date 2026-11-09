// G3 hit volumes (STATE-g3g5 V1, V2, E1): each ring of the 230 moves.json moves, as the JS engine
// exports it (clip-markers.json), becomes a capsule bound to the swing rig's blade bone.
//  - V1: reach (tip distance + radius, from the posed skeleton) within 5% of the ring's r1;
//  - V2: the unwrapped yaw span of the tip within 5% of arc1 - arc0, each end within 5% of the span;
//  - planted: bound to `swing` instead of `blade`; length x1.06; yaw mirrored. Each must fail.
//  - E1 (exploratory, no bound): ring and capsule contact over a target grid, and first-contact delay.
#include "action_tables.hpp"
#include "check.hpp"
#include "raw/game/action/hitvol.hpp"
#include <cmath>

using namespace action_test;

struct Err { double reach = 0, span = 0, ends = 0; };
static Err error_of(const HitMarker& h, const Sweep& s) {
    const double span = h.arc[1] - h.arc[0];
    Err e;
    e.reach = std::fabs(s.reach - h.r[1]) / h.r[1];
    e.span = std::fabs((s.arc_hi - s.arc_lo) - span) / span;
    e.ends = std::max(std::fabs(s.arc_lo - h.arc[0]), std::fabs(s.arc_hi - h.arc[1])) / span;
    return e;
}

struct Tally { int rings = 0, reach_bad = 0, arc_bad = 0; Err worst; };
static Tally tally(const std::vector<const HitMarker*>& rings, const RingOpts& o, bool mirror) {
    Tally t;
    for (const HitMarker* h : rings) {
        const Err e = error_of(*h, measure_sweep(volume_from_ring(*h, o), 0, mirror));
        ++t.rings;
        t.reach_bad += e.reach > 0.05;
        t.arc_bad += e.span > 0.05 || e.ends > 0.05;
        t.worst.reach = std::max(t.worst.reach, e.reach); t.worst.span = std::max(t.worst.span, e.span); t.worst.ends = std::max(t.worst.ends, e.ends);
    }
    return t;
}

// E1: targets at distance 0.05..r1+R in 0.1 m steps and angle -180..175 in 5 degree steps.
static void contact_grid(const std::vector<const HitMarker*>& rings) {
    const double R = 0.35, H = 1.8;
    long agree = 0, total = 0, ring_only = 0, cap_only = 0, delayed = 0, both = 0; double delay = 0;
    for (const HitMarker* h : rings) {
        const HitVolume v = volume_from_ring(*h);
        std::vector<std::vector<Segment>> segs;
        for (int f = v.on; f <= v.off; ++f) segs.push_back(sweep_segments(v, f));
        for (double d = 0.05; d <= h->r[1] + R; d += 0.1)
            for (int deg = -180; deg < 180; deg += 5) {
                const double x = d * std::cos(deg * 3.14159265358979323846 / 180), z = d * std::sin(deg * 3.14159265358979323846 / 180);
                const bool ring = ring_hits(*h, x, z, R, H);
                int first = -1;
                for (size_t i = 0; i < segs.size() && first < 0; ++i) if (segments_hit(v, segs[i], x, z, R, H)) first = (int)i;
                ++total; agree += ring == (first >= 0); ring_only += ring && first < 0; cap_only += !ring && first >= 0;
                if (ring && first >= 0) { ++both; delay += first; delayed += first > 0; }
            }
    }
    std::printf("E1: %ld targets, %.2f%% agree; ring only %ld, capsule only %ld; both %ld, capsule later in %ld, mean delay %.2f frames\n",
                total, 100.0 * agree / total, ring_only, cap_only, both, delayed, both ? delay / both : 0.0);
}

int main() {
    const MarkerSet ms = load_markers(slurp(DIR + "clip-markers.json"));
    const Tables t = load_tables();
    std::vector<const HitMarker*> rings;
    for (const MarkerClip& c : ms.clips) if (t.find(c.key)) for (const HitMarker& h : c.hits) rings.push_back(&h);
    const Tally base = tally(rings, {}, false);
    std::printf("V1/V2: %d rings; reach over 5%%: %d, arc over 5%%: %d; worst reach %.4f%%, span %.4f%%, ends %.4f%%\n",
                base.rings, base.reach_bad, base.arc_bad, 100 * base.worst.reach, 100 * base.worst.span, 100 * base.worst.ends);
    CHECK(base.rings == 179 && base.reach_bad == 0 && base.arc_bad == 0);
    RingOpts on_swing; on_swing.bone = swing_rig().swing;
    const Tally a = tally(rings, on_swing, false);
    std::printf("planted bound to swing: reach fails %d of %d, arc fails %d\n", a.reach_bad, a.rings, a.arc_bad);
    CHECK(a.reach_bad > 0);
    RingOpts longer; longer.length_scale = 1.06;
    const Tally b = tally(rings, longer, false);
    std::printf("planted length x1.06: reach fails %d of %d\n", b.reach_bad, b.rings);
    CHECK(b.reach_bad > 0);
    const Tally m = tally(rings, {}, true);
    int asym = 0;
    for (const HitMarker* h : rings) asym += std::fabs(h->arc[0] + h->arc[1]) > 1e-9;
    std::printf("planted yaw mirrored: arc fails %d of %d (asymmetric arcs: %d)\n", m.arc_bad, m.rings, asym);
    CHECK(m.arc_bad > 0);
    contact_grid(rings);
    return raw_test_summary();
}
