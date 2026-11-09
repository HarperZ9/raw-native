// Capsule contact on the shared math: see raw/game/action/contact.hpp. Every expression below is written in the
// order of Low Water's combat/sim/capsule.mjs; change one only together with the other.
#include "raw/game/action/contact.hpp"
#include "raw/game/action/detmath.hpp"
#include <algorithm>
#include <cmath>

namespace raw::game::action {
namespace {
constexpr double DEG = 180 / DPI;
}

int ring_substeps(const Ring& h) {
    const double per = std::fabs(h.arc[1] - h.arc[0]) / std::max(1, h.off + 1 - h.on);
    return std::max(8, (int)std::ceil(per / CONTACT_MAX_STEP_DEG));
}

double ring_yaw(const Ring& h, int f, int k, int n) {
    const double num = (double)(f - h.on) * n + k, den = (double)(h.off + 1 - h.on) * n;
    return h.arc[0] + (h.arc[1] - h.arc[0]) * (num / den);
}

double blade_dist2(const Ring& h, double yaw, double px, double pz, double* s, double rc) {
    const double a = h.r[0] + rc, len = std::max(0.0, h.r[1] - h.r[0] - 2 * rc), b = a + len;
    const double rad = yaw / DEG, c = dcos(rad), sn = dsin(rad);
    const double ax = a * c, az = a * sn, ux = b * c - ax, uz = b * sn - az, wx = px - ax, wz = pz - az;
    const double l2 = ux * ux + uz * uz, t = l2 > 0 ? std::min(1.0, std::max(0.0, (wx * ux + wz * uz) / l2)) : 0.0;
    const double ex = wx - t * ux, ez = wz - t * uz;
    if (s) *s = t;
    return ex * ex + ez * ez;
}

bool capsule_contact(const Body& A, const Ring& h, const Body& T, int f, ContactAt* at, double rc) {
    if (h.y[1] < 0 || h.y[0] > T.height) return false;
    const double dx = T.x - A.x, dz = T.z - A.z, cf = dcos(A.face), sf = dsin(A.face);
    const double px = dx * cf + dz * sf, pz = dz * cf - dx * sf, reach = T.radius + rc, reach2 = reach * reach;
    const int n = ring_substeps(h);
    for (int k = 0; k <= n; k++) {
        double s = 0;
        const double yaw = ring_yaw(h, f, k, n);
        if (blade_dist2(h, yaw, px, pz, &s, rc) > reach2) continue;
        if (at) {
            const double a = h.r[0] + rc, len = std::max(0.0, h.r[1] - h.r[0] - 2 * rc);
            const double rad = yaw / DEG, c = dcos(rad), sn = dsin(rad), r = a + s * len;
            const double lx = r * c, lz = r * sn;
            at->sample = k;
            at->x = A.x + lx * cf - lz * sf; at->z = A.z + lx * sf + lz * cf;
            blade_tip(A, h, f, k, n, at->tip_x, at->tip_z);
        }
        return true;
    }
    return false;
}

void blade_tip(const Body& A, const Ring& h, int f, int k, int n, double& x, double& z) {
    const double a = h.r[0] + CONTACT_RC, b = a + std::max(0.0, h.r[1] - h.r[0] - 2 * CONTACT_RC);
    const double rad = ring_yaw(h, f, k, n) / DEG, lx = b * dcos(rad), lz = b * dsin(rad);
    const double cf = dcos(A.face), sf = dsin(A.face);
    x = A.x + lx * cf - lz * sf; z = A.z + lx * sf + lz * cf;
}

}  // namespace raw::game::action
