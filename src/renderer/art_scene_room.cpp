// The conversation room: see raw/renderer/art_room.hpp. A board on a table, a dais for the poster,
// and one figure a reply account (a cylinder body and a sphere head, as in the town), traced in
// float64 by the art scene's primitives. Each group sits inside one box tested first, so a ray
// that misses a group skips its figures.
#include "raw/renderer/art_room.hpp"
#include "art_scene_prims.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace raw::art {
namespace {
using namespace prims;
constexpr double kTable = -0.4, kTableHalf = 90.0;
constexpr Box kBoard{{-15.0, kTable, -10.0}, {15.0, 0.0, 8.0}, 2, 1, {0.50, 0.46, 0.39}};
constexpr Box kDais{{-1.8, 0.0, -8.4}, {1.8, 0.55, -6.2}, 3, 2, {0.30, 0.29, 0.28}};
constexpr D3 kTableAlbedo{0.10, 0.075, 0.06};
constexpr D3 kSkin{0.62, 0.50, 0.42};
// The verdict colours of the Daylight canon (src/ext/canon.css, light): drift and ink, in linear.
constexpr D3 kDrift{0.27, 0.042, 0.010}, kInk{0.008, 0.007, 0.005}, kDiscRest{0.42, 0.39, 0.33};
constexpr double kBodyR = 0.24, kBodyTop = 1.45, kHeadR = 0.165, kDiscR = 0.52, kDiscH = 0.035;

std::uint32_t lcg(std::uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
double unit(std::uint32_t& s) { return double(lcg(s) >> 8) / double(1u << 24); }

RoomState g_state;

std::vector<RoomFigure> build() {
    std::vector<RoomFigure> v;
    int obj = 40;
    v.push_back({{0.0, 0.55, -7.3}, {0.20, 0.24, 0.30}, RoomGroup::Poster, -1, obj++});
    // Ordinary repliers: a loose crowd with muted, varied coats, kept apart by rejection.
    std::uint32_t s = 20261022u;
    const D3 coats[] = {{0.42, 0.20, 0.16}, {0.30, 0.34, 0.40}, {0.24, 0.30, 0.22}, {0.50, 0.44, 0.30}, {0.26, 0.24, 0.30}, {0.58, 0.52, 0.42}, {0.36, 0.28, 0.24}};
    std::vector<D3> taken;
    auto clear = [&](const D3& p, double r) { for (const D3& q : taken) if (std::hypot(p[0] - q[0], p[2] - q[2]) < r) return false; return true; };
    int tries = 0;
    while (int(v.size()) < 1 + 26 && tries++ < 4000) {
        const D3 p{-12.5 + 13.0 * unit(s), 0.0, -4.6 + 9.4 * unit(s)};
        if (!clear(p, 1.25)) continue;
        taken.push_back(p);
        v.push_back({p, coats[lcg(s) % 7], RoomGroup::Ordinary, -1, obj++});
    }
    // Disclosed paid voices near the front of the crowd, and three look-alikes beside them.
    const D3 paid[] = {{-3.2, 0, -3.9}, {-1.1, 0, -4.4}, {1.0, 0, -3.6}, {-2.0, 0, -2.2}};
    for (const D3& p : paid) v.push_back({p, {0.44, 0.40, 0.36}, RoomGroup::Paid, -1, obj++});
    const D3 look[] = {{2.6, 0, -1.8}, {3.4, 0, -3.6}, {1.6, 0, -0.4}};
    for (const D3& p : look) v.push_back({p, {0.44, 0.40, 0.36}, RoomGroup::Lookalike, -1, obj++});
    // The burst: 36 identical figures in a tight block, arriving within a few seconds of each other.
    std::uint32_t b = 7u;
    for (int i = 0; i < 36; ++i) {
        const int r = i / 6, c = i % 6;
        const D3 p{5.4 + 1.0 * c + 0.12 * (unit(b) - 0.5), 0.0, -0.8 + 1.0 * r + 0.12 * (unit(b) - 0.5)};
        v.push_back({p, {0.47, 0.47, 0.46}, RoomGroup::Burst, 0.075 * double((i * 7) % 36), obj++});
    }
    return v;
}

struct Bounds { D3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9}; };
const std::vector<Bounds>& groupBounds() {
    static const std::vector<Bounds> b = [] {
        std::vector<Bounds> o(5);
        for (const RoomFigure& f : roomFigures()) {
            Bounds& g = o[std::size_t(f.group)];
            for (int k = 0; k < 3; k += 2) {
                g.lo[std::size_t(k)] = std::min(g.lo[std::size_t(k)], f.foot[std::size_t(k)] - kDiscR);
                g.hi[std::size_t(k)] = std::max(g.hi[std::size_t(k)], f.foot[std::size_t(k)] + kDiscR);
            }
            g.lo[1] = std::min(g.lo[1], f.foot[1]);
            g.hi[1] = std::max(g.hi[1], f.foot[1] + kPersonHeight + (f.group == RoomGroup::Burst ? 8.0 : 0.0));
        }
        return o;
    }();
    return b;
}
bool slab(const Bounds& b, const D3& o, const D3& d, double tBest) {
    double t0 = 0, t1 = tBest < 0 ? 1e300 : tBest;
    for (std::size_t k = 0; k < 3; ++k) {
        if (std::abs(d[k]) < 1e-15) { if (o[k] < b.lo[k] || o[k] > b.hi[k]) return false; continue; }
        double ta = (b.lo[k] - o[k]) / d[k], tb = (b.hi[k] - o[k]) / d[k];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}
D3 mix(const D3& a, const D3& b, double t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }
}  // namespace

const std::vector<RoomFigure>& roomFigures() {
    static const std::vector<RoomFigure> v = build();
    return v;
}

bool roomFootAt(const RoomFigure& f, const RoomState& s, D3& foot) {
    foot = f.foot;
    if (f.arrive < 0) return true;
    const double at = s.burstAt + f.arrive;
    if (s.t < at) return false;
    // The drop: eased out of the air, a stop-motion set-down.
    const double u = std::clamp((s.t - at) / kRoomDrop, 0.0, 1.0), e = 1 - (1 - u) * (1 - u) * (1 - u);
    foot[1] += 6.0 * (1 - e);
    return true;
}

void setRoomState(const RoomState& s) { g_state = s; }
const RoomState& roomState() { return g_state; }

Hit traceRoom(const D3& o, const D3& d) {
    Hit best;
    if (std::abs(d[1]) > 1e-15) {
        const double t = (kTable - o[1]) / d[1];
        const double x = o[0] + t * d[0], z = o[2] + t * d[2];
        if (std::abs(x) <= kTableHalf && std::abs(z) <= kTableHalf) consider(best, t, {0, 1, 0}, 1, 1, kTableAlbedo);
    }
    box(kBoard, o, d, best);
    box(kDais, o, d, best);
    const RoomState& s = g_state;
    const auto& gb = groupBounds();
    for (const RoomFigure& f : roomFigures()) {
        if (!slab(gb[std::size_t(f.group)], o, d, best.t)) continue;
        D3 p;
        if (!roomFootAt(f, s, p)) continue;
        cylinder({p[0], p[2], kBodyR, p[1], p[1] + kBodyTop, f.obj, 11, f.albedo}, o, d, best);
        sphere({{p[0], p[1] + kPersonHeight - kHeadR, p[2]}, kHeadR, f.obj, 12, kSkin}, o, d, best);
        if (f.group == RoomGroup::Paid || f.group == RoomGroup::Lookalike) {
            const double m = f.group == RoomGroup::Paid ? s.paidMark : s.lookalikeMark;
            const D3 mark = mix(kDiscRest, f.group == RoomGroup::Paid ? kDrift : kInk, std::clamp(m, 0.0, 1.0));
            cylinder({p[0], p[2], kDiscR, p[1], p[1] + kDiscH, f.obj, 13, mark}, o, d, best);
        }
    }
    return best;
}

D3 roomCentre(RoomGroup g) {
    D3 c{0, 0, 0};
    int n = 0;
    for (const RoomFigure& f : roomFigures()) if (f.group == g) { for (std::size_t k = 0; k < 3; ++k) c[k] += f.foot[k]; ++n; }
    if (n) for (std::size_t k = 0; k < 3; ++k) c[k] /= n;
    c[1] += 0.9;
    return c;
}

Lighting roomLight() {
    // A high, warm studio key from the left and a pale cool fill: the model on a work table.
    Lighting L = daylight();
    L.sun.dir = {-0.42, 0.84, 0.34};
    const double l = std::sqrt(L.sun.dir[0] * L.sun.dir[0] + L.sun.dir[1] * L.sun.dir[1] + L.sun.dir[2] * L.sun.dir[2]);
    for (double& x : L.sun.dir) x /= l;
    L.sun.colour = {1.55, 1.42, 1.22};
    L.skyTop = {0.24, 0.27, 0.32}; L.skyHorizon = {0.34, 0.32, 0.30}; L.ground = {0.10, 0.085, 0.07};
    return L;
}

}  // namespace raw::art
