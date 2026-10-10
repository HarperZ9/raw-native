// Clustered light assignment, brute force: see raw/renderer/lighting.hpp.
#include "raw/renderer/lighting.hpp"
#include <algorithm>
#include <cmath>
namespace raw::lighting {

int ClusterGrid::clusterOf(D3 p) const {
    const double d = -p.z;
    if (!(d >= nearZ && d < farZ)) return -1;
    const double th = std::tan(0.5 * fovy);
    const double nx = p.x / (d * th * aspect), ny = p.y / (d * th);
    if (!(std::fabs(nx) <= 1.0 && std::fabs(ny) <= 1.0)) return -1;
    const int ix = std::min(int((nx + 1.0) * 0.5 * kX), kX - 1), iy = std::min(int((ny + 1.0) * 0.5 * kY), kY - 1);
    const int iz = std::min(int(std::log(d / nearZ) / std::log(farZ / nearZ) * kZ), kZ - 1);
    return (iz * kY + iy) * kX + ix;
}

void ClusterGrid::bounds(int c, D3& lo, D3& hi) const {
    const int ix = c % kX, iy = (c / kX) % kY, iz = c / (kX * kY);
    const double th = std::tan(0.5 * fovy);
    const double x0 = -1.0 + 2.0 * ix / kX, x1 = -1.0 + 2.0 * (ix + 1) / kX;
    const double y0 = -1.0 + 2.0 * iy / kY, y1 = -1.0 + 2.0 * (iy + 1) / kY;
    const double d0 = nearZ * std::pow(farZ / nearZ, double(iz) / kZ), d1 = nearZ * std::pow(farZ / nearZ, double(iz + 1) / kZ);
    lo = {1e300, 1e300, 1e300}; hi = {-1e300, -1e300, -1e300};
    for (double d : {d0, d1}) for (double nx : {x0, x1}) for (double ny : {y0, y1}) {
        const D3 q{nx * d * th * aspect, ny * d * th, -d};
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
        hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
    }
}

// Arvo 1990: the squared distance from the sphere's centre to the box, against range^2.
bool lightTouchesBox(const Light& l, D3 lo, D3 hi) {
    if (l.type == LightType::Directional || l.range <= 0.0) return true;
    const auto axis = [](double c, double a, double b) { return c < a ? (a - c) * (a - c) : c > b ? (c - b) * (c - b) : 0.0; };
    const double d2 = axis(l.position.x, lo.x, hi.x) + axis(l.position.y, lo.y, hi.y) + axis(l.position.z, lo.z, hi.z);
    return d2 <= l.range * l.range;
}

std::vector<std::vector<int>> assignClusters(const ClusterGrid& g, const std::vector<Light>& lights, int* overflow) {
    std::vector<std::vector<int>> out(std::size_t(g.count()));
    int cut = 0;
    for (int c = 0; c < g.count(); ++c) {
        D3 lo, hi;
        g.bounds(c, lo, hi);
        for (std::size_t k = 0; k < lights.size(); ++k) {
            if (!lightTouchesBox(lights[k], lo, hi)) continue;
            if (int(out[std::size_t(c)].size()) < ClusterGrid::kMaxPerCluster) out[std::size_t(c)].push_back(int(k));
            else ++cut;
        }
    }
    if (overflow) *overflow = cut;
    return out;
}

}  // namespace raw::lighting
