// raw/game/action/hitvol: hit volumes as capsules bound to bones, live on clip markers (G3).
//
// A game's hit geometry often comes as a ring sector around the attacker (Low Water's
// combat/sim/hits.mjs: an arc in degrees from facing, radii r0..r1, a height band, live as a whole on
// the frames of the hit's window). volume_from_ring turns one ring into one capsule bound to the
// `blade` bone of a three-joint swing rig (root, swing at the band's mid height, blade at r0 + rc
// along the swing), with a raw::anim clip that yaws the swing from arc0 at the hit's on marker to arc1
// at off + 1. Live tick f tests the capsule swept over clip time [f, f + 1), so a one-frame window still
// sweeps the whole arc. The bones are posed with raw::anim::poseSkeleton (raw/scene/skin.hpp).
//
// Contacts are tested in the ground plane against an upright cylinder (the hurt volume), and the
// ring's height band against the cylinder's height, as the ring test does. The capsule's own vertical
// extent is not used: this rig stands in for the art's bones until a real skeleton carries the blade.
// Attacker space: facing +x, angles counterclockwise from +x toward +z (atan2(z, x)). Layer: game.
#pragma once
#include "raw/game/action/markers.hpp"
#include "raw/scene/clip.hpp"
#include "raw/scene/skin.hpp"
#include <vector>

namespace raw::game::action {

struct Capsule { int bone = 0; raw::anim::Vec3d a{0, 0, 0}, b{0, 0, 0}; double radius = 0.05; };

struct HitVolume {
    Capsule cap;
    int on = 0, off = -1;                // live sim ticks, inclusive
    double height = 1.1;                 // the swing joint's height
    double y[2] = {0, 2.2};              // the height band the contact test uses
    raw::anim::Clip yaw;                 // channels: yaw (degrees), blade offset (m), over clip frames
    int substeps = 8;                    // samples a tick: the yaw moves at most max_step_deg between two
};

struct SwingRig { raw::anim::Skeleton sk; int root = 0, swing = 1, blade = 2; };
// Built once: root, swing (child of root), blade (child of swing).
const SwingRig& swing_rig();

struct RingOpts {
    double radius = 0.05;                // capsule radius
    int bone = 2;                        // planted checks bind elsewhere
    double length_scale = 1;             // planted checks stretch the capsule
    double max_step_deg = 4;             // substeps a tick = max(8, ceil(yaw a tick / max_step_deg))
};
HitVolume volume_from_ring(const HitMarker& h, const RingOpts& o = {});

// Capsule end points in attacker space at clip frame t. mirror: yaw about +y (a planted check).
struct Segment { raw::anim::Vec3d a, b; };
Segment capsule_at(const HitVolume& v, double t, bool mirror = false);

// Over the live window, sampled `substeps` times a tick from on to off + 1 inclusive.
struct Sweep { double reach = 0, arc_lo = 0, arc_hi = 0; int samples = 0; };
Sweep measure_sweep(const HitVolume& v, int substeps = 0, bool mirror = false);   // 0: v.substeps

// The capsule's segments over tick f: clip time f + k / substeps, k = 0..substeps.
std::vector<Segment> sweep_segments(const HitVolume& v, int tick, int substeps = 0);
// Whether those segments touch an upright cylinder at (x, z), radius R, height H (the band too).
bool segments_hit(const HitVolume& v, const std::vector<Segment>& segs, double x, double z, double R, double H);
// The capsule sweep of tick f against an upright cylinder at (x, z), radius R, height H.
bool capsule_hits(const HitVolume& v, int tick, double x, double z, double R, double H, int substeps = 0);
// The ring test of combat/sim/hits.mjs overlaps (standard math, not the shared bit-exact math).
bool ring_hits(const HitMarker& h, double x, double z, double R, double H);

}  // namespace raw::game::action
