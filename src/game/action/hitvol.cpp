// Hit volumes: see raw/game/action/hitvol.hpp.
#include "raw/game/action/hitvol.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace raw::game::action {
namespace {

constexpr double PI = 3.14159265358979323846, DEG = 180 / PI;

raw::anim::Skeleton make_rig() {
    std::string err;
    raw::anim::Skeleton sk = raw::anim::makeSkeleton({{"root", -1}, {"swing", 0}, {"blade", 1}}, err);
    if (!err.empty()) throw std::runtime_error("hitvol: " + err);
    return sk;
}

double wrap_deg(double a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

// Squared distance from point p to segment ab in the ground plane (x, z).
double dist2_xz(const raw::anim::Vec3d& a, const raw::anim::Vec3d& b, double px, double pz) {
    const double ux = b[0] - a[0], uz = b[2] - a[2], wx = px - a[0], wz = pz - a[2];
    const double l2 = ux * ux + uz * uz;
    const double s = l2 > 0 ? std::clamp((wx * ux + wz * uz) / l2, 0.0, 1.0) : 0.0;
    const double dx = wx - s * ux, dz = wz - s * uz;
    return dx * dx + dz * dz;
}

}  // namespace

const SwingRig& swing_rig() {
    static const SwingRig rig{make_rig(), 0, 1, 2};
    return rig;
}

HitVolume volume_from_ring(const HitMarker& h, const RingOpts& o) {
    HitVolume v;
    v.on = h.on; v.off = h.off;
    v.y[0] = h.y[0]; v.y[1] = h.y[1];
    v.height = (h.y[0] + h.y[1]) / 2;
    const double len = std::max(0.0, h.r[1] - h.r[0] - 2 * o.radius) * o.length_scale;
    v.cap.bone = o.bone; v.cap.radius = o.radius;
    v.cap.a = {0, 0, 0}; v.cap.b = {len, 0, 0};
    // the blade joint sits at r0 + rc along the swing; the capsule runs from there to r1 - rc
    v.yaw.loop = false;
    v.yaw.length = h.off + 1.0;
    const double per_tick = std::fabs(h.arc[1] - h.arc[0]) / std::max(1, h.off + 1 - h.on);
    v.substeps = std::max(8, (int)std::ceil(per_tick / o.max_step_deg));
    v.yaw.keys = {{(double)h.on, {h.arc[0], h.r[0] + o.radius}}, {h.off + 1.0, {h.arc[1], h.r[0] + o.radius}}};
    return v;
}

Segment capsule_at(const HitVolume& v, double t, bool mirror) {
    const SwingRig& rig = swing_rig();
    const std::vector<double> ch = raw::anim::sampleClip(v.yaw, t);
    raw::anim::Pose pose(rig.sk.joints.size());
    pose[(size_t)rig.swing].hasR = true;
    pose[(size_t)rig.swing].r = raw::anim::quatAxisAngle({0, mirror ? 1.0 : -1.0, 0}, ch[0] / DEG);
    pose[(size_t)rig.swing].hasT = true;
    pose[(size_t)rig.swing].t = {0, v.height, 0};
    pose[(size_t)rig.blade].hasT = true;
    pose[(size_t)rig.blade].t = {ch[1], 0, 0};
    const std::vector<raw::anim::Mat> world = raw::anim::poseSkeleton(rig.sk, pose);
    const raw::anim::Mat& m = world.at((size_t)v.cap.bone);
    return {raw::anim::apply(m, v.cap.a), raw::anim::apply(m, v.cap.b)};
}

Sweep measure_sweep(const HitVolume& v, int substeps, bool mirror) {
    Sweep s;
    if (substeps <= 0) substeps = v.substeps;
    const int n = substeps * (v.off + 1 - v.on);
    double prev = 0, unwrapped = 0;
    for (int k = 0; k <= n; ++k) {
        const Segment g = capsule_at(v, v.on + (double)k / substeps, mirror);
        s.reach = std::max(s.reach, std::hypot(g.b[0], g.b[2]) + v.cap.radius);
        const double ang = std::atan2(g.b[2], g.b[0]) * DEG;
        unwrapped = k == 0 ? ang : unwrapped + wrap_deg(ang - prev);
        prev = ang;
        s.arc_lo = k == 0 ? unwrapped : std::min(s.arc_lo, unwrapped);
        s.arc_hi = k == 0 ? unwrapped : std::max(s.arc_hi, unwrapped);
        ++s.samples;
    }
    return s;
}

std::vector<Segment> sweep_segments(const HitVolume& v, int tick, int substeps) {
    std::vector<Segment> out;
    if (substeps <= 0) substeps = v.substeps;
    for (int k = 0; k <= substeps; ++k) out.push_back(capsule_at(v, tick + (double)k / substeps));
    return out;
}

bool segments_hit(const HitVolume& v, const std::vector<Segment>& segs, double x, double z, double R, double H) {
    if (v.y[1] < 0 || v.y[0] > H) return false;
    const double reach2 = (R + v.cap.radius) * (R + v.cap.radius);
    for (const Segment& g : segs) if (dist2_xz(g.a, g.b, x, z) <= reach2) return true;
    return false;
}

bool capsule_hits(const HitVolume& v, int tick, double x, double z, double R, double H, int substeps) {
    if (tick < v.on || tick > v.off) return false;
    return segments_hit(v, sweep_segments(v, tick, substeps), x, z, R, H);
}

bool ring_hits(const HitMarker& h, double x, double z, double R, double H) {
    const double d = std::hypot(x, z);
    if (d - R > h.r[1] || d + R < h.r[0]) return false;
    if (h.y[1] < 0 || h.y[0] > H) return false;
    if (d < 1e-6) return true;
    const double ang = wrap_deg(std::atan2(z, x) * DEG), pad = std::asin(std::min(1.0, R / d)) * DEG;
    return ang >= h.arc[0] - pad && ang <= h.arc[1] + pad;
}

}  // namespace raw::game::action
