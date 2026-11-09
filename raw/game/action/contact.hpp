// raw/game/action/contact: the swing decides the hit. A move's ring (raw/game/action/moves.hpp Ring) is the
// blade of the G3 swing rig (hitvol.hpp): a capsule of radius RC from r0 + RC to r1 - RC along a bone that yaws
// from arc0 on the first live frame to arc1 at the last live frame + 1. Live frame f tests the sweep over
// [f, f + 1) at n = max(8, ceil(yaw a frame / 4 degrees)) samples against the target's upright cylinder in the
// ground plane (radius + RC), and the ring's height band against the body's height.
//
// Unlike hitvol.hpp (standard math, a posed skeleton), this kernel runs on the shared bit-exact math
// (detmath.hpp) in the expression order of Low Water's combat/sim/capsule.mjs, so the C++ and the JS find the
// same contacts on the same tick, bit for bit. hitvol.hpp stays the reference geometry: the two agree on the
// G3 grid except within rounding of the contact boundary (tests/test_action_contact.cpp). Layer: game.
#pragma once
#include "raw/game/action/moves.hpp"

namespace raw::game::action {

inline constexpr double CONTACT_RC = 0.05, CONTACT_MAX_STEP_DEG = 4;

// A body in the ground plane: position, facing (radians, counterclockwise from +x toward +z), hurt cylinder.
struct Body { double x = 0, z = 0, face = 0, radius = 0.35, height = 1.8; };

int ring_substeps(const Ring& h);
double ring_yaw(const Ring& h, int f, int k, int n);                 // degrees, at clip time f + k / n
// Squared distance from (px, pz) in attacker space to the blade at `yaw`; `s` gets the blade parameter 0..1.
double blade_dist2(const Ring& h, double yaw, double px, double pz, double* s = nullptr, double rc = CONTACT_RC);

struct ContactAt {
    int sample = -1;            // the first substep that touched, 0..n
    double x = 0, z = 0;        // the blade point nearest the target at that substep, world space
    double tip_x = 0, tip_z = 0;
};
// Whether ring h of attacker A's move, on the move's frame f, touches T. `at` gets where, when it does.
bool capsule_contact(const Body& A, const Ring& h, const Body& T, int f, ContactAt* at = nullptr, double rc = CONTACT_RC);
// The blade tip (r1 - RC along the bone) in world space at clip time f + k / n.
void blade_tip(const Body& A, const Ring& h, int f, int k, int n, double& x, double& z);

}  // namespace raw::game::action
