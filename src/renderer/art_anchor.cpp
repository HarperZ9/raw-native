// P5's world anchor: see raw/renderer/art_anchor.hpp.
#include "raw/renderer/art_anchor.hpp"
#include <cmath>
namespace raw::art {

AnchorParams anchorParams(const View& v) {
    const double dx = v.eye[0] - v.target[0], dy = v.eye[1] - v.target[1], dz = v.eye[2] - v.target[2];
    AnchorParams a;
    a.focal = v.focalPx();
    a.scale = a.focal / std::sqrt(dx * dx + dy * dy + dz * dz);
    return a;
}

namespace {
// The chart of one pixel: face 0 (y dominant), 1 (x), 2 (z), or 3 (sky by direction).
void chart(double px, double py, double pz, double nx, double ny, double nz, bool sky, const AnchorParams& a, double& u, double& v) {
    const double ax = std::abs(nx), ay = std::abs(ny), az = std::abs(nz);
    if (sky) {
        double m = az, p = px, q = py;   // here (px, py, pz) is the view direction
        if (ay >= ax && ay >= az) { m = ay; p = px; q = pz; }
        else if (ax >= az) { m = ax; p = pz; q = py; }
        u = p / m * a.focal + kAnchorOffset[3][0];
        v = q / m * a.focal + kAnchorOffset[3][1];
        return;
    }
    int face = 2;
    double p = px, q = py;
    if (ay >= ax && ay >= az) { face = 0; p = px; q = pz; }
    else if (ax >= az) { face = 1; p = pz; q = py; }
    u = p * a.scale + kAnchorOffset[face][0];
    v = q * a.scale + kAnchorOffset[face][1];
}
}  // namespace

std::vector<float> anchorCanvas(const Frame& f, const AnchorParams& a) {
    const std::size_t n = std::size_t(f.w) * std::size_t(f.h);
    std::vector<float> C(n * 4, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const bool sky = f.dist[i] < 0;
        const float* P = sky ? &f.dir[i * 3] : &f.P[i * 3];
        const float* N = sky ? &f.dir[i * 3] : &f.N[i * 3];
        double u = 0, v = 0;
        chart(P[0], P[1], P[2], N[0], N[1], N[2], sky, a, u, v);
        C[i * 4] = float(u);
        C[i * 4 + 1] = float(v);
    }
    return C;
}

style::PaintCanvas worldCanvas(int w, int h, const std::vector<float>& coords) {
    style::PaintCanvas cv(w, h);
    cv.C = coords;
    cv.layer[0].age = 1e6;
    cv.layer[0].meanD = 0;
    cv.layer[1].age = -1;
    return cv;
}

}  // namespace raw::art
