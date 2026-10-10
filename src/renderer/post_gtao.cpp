// The view-space G-buffer, GTAO and its ray-cast reference: see raw/renderer/post.hpp.
#include "raw/renderer/post.hpp"
#include "raw/renderer/bvh.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::post {
namespace {
constexpr double kPi = 3.14159265358979323846;
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }
double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void at(const std::vector<float>& v, std::size_t i, double out[3]) { out[0] = v[i * 3]; out[1] = v[i * 3 + 1]; out[2] = v[i * 3 + 2]; }

// 1 / depth interpolated bilinearly between the four nearest pixel centres (exact on a plane);
// the nearest pixel's depth when one of them is empty or they differ by more than 10%.
double sampleDepth(const ViewGBuffer& v, double fx, double fy, int sx, int sy) {
    const double nearest = v.depth[std::size_t(sy) * v.w + sx];
    const int x0 = int(std::floor(fx - 0.5)), y0 = int(std::floor(fy - 0.5));
    const double ax = fx - 0.5 - x0, ay = fy - 0.5 - y0;
    double k[4], lo = 1e30, hi = 0.0;
    for (int q = 0; q < 4; ++q) {
        const int qx = x0 + (q & 1), qy = y0 + (q >> 1);
        if (!v.valid(qx, qy)) return nearest;
        const double d = v.depth[std::size_t(qy) * v.w + qx];
        lo = std::min(lo, d); hi = std::max(hi, d);
        k[q] = 1.0 / d;
    }
    if (hi > 1.1 * lo) return nearest;
    const double inv = (k[0] * (1 - ax) + k[1] * ax) * (1 - ay) + (k[2] * (1 - ax) + k[3] * ax) * ay;
    return 1.0 / inv;
}
}  // namespace

ViewGBuffer viewGBuffer(const GBuffer& g, const Camera& cam) {
    ViewGBuffer v;
    v.w = g.depth.w; v.h = g.depth.h;
    v.tanHalf = std::tan(0.5 * double(cam.fovy)); v.aspect = cam.aspect; v.nearZ = cam.nearZ;
    const Mat4 view = cam.view();
    const std::size_t n = std::size_t(v.w) * v.h;
    v.pos.assign(n * 3, 0.0f); v.nrm.assign(n * 3, 0.0f); v.depth.assign(n, -1.0f);
    for (int y = 0; y < v.h; ++y) for (int x = 0; x < v.w; ++x) {
        if (!g.mask.at(x, y)) continue;
        const std::size_t i = std::size_t(y) * v.w + x;
        const Vec3 p = g.position.at(x, y), q = g.normal.at(x, y);
        const Vec4 pv = mul(view, Vec4{p.x, p.y, p.z, 1.0f}), nv = mul(view, Vec4{q.x, q.y, q.z, 0.0f});
        const Vec3 nn = normalize(Vec3{nv.x, nv.y, nv.z});
        v.pos[i * 3] = pv.x; v.pos[i * 3 + 1] = pv.y; v.pos[i * 3 + 2] = pv.z;
        v.nrm[i * 3] = nn.x; v.nrm[i * 3 + 1] = nn.y; v.nrm[i * 3 + 2] = nn.z;
        v.depth[i] = -pv.z;
    }
    return v;
}

bool project(const ViewGBuffer& v, const double p[3], double& sx, double& sy) {
    if (p[2] >= -1e-6) return false;
    const double w = -p[2];
    sx = (p[0] / (w * v.tanHalf * v.aspect) * 0.5 + 0.5) * v.w;
    sy = (1.0 - (p[1] / (w * v.tanHalf) * 0.5 + 0.5)) * v.h;
    return true;
}

// Jimenez et al. 2016: per slice, the two horizon angles from the view vector, clamped to the
// hemisphere of the projected normal, integrated against the cosine (their eq. 10).
std::vector<double> gtao(const ViewGBuffer& v, const GtaoParams& prm) {
    std::vector<double> out(std::size_t(v.w) * v.h, 1.0);
    parallelRows(v.h, threads(), [&](int y) {
        for (int x = 0; x < v.w; ++x) {
            const std::size_t i = std::size_t(y) * v.w + x;
            if (v.depth[i] < 0.0f) continue;
            double P[3], N[3];
            at(v.pos, i, P); at(v.nrm, i, N);
            const double pl = std::sqrt(dot3(P, P));
            const double V[3] = {-P[0] / pl, -P[1] / pl, -P[2] / pl};
            const double rpx = prm.radius * v.h / (2.0 * v.tanHalf * v.depth[i]);
            // A basis perpendicular to the view vector: T toward screen x, B = V x T.
            double T[3] = {1.0 - V[0] * V[0], -V[0] * V[1], -V[0] * V[2]};
            const double tl = std::sqrt(dot3(T, T));
            T[0] /= tl; T[1] /= tl; T[2] /= tl;
            const double B[3] = {V[1] * T[2] - V[2] * T[1], V[2] * T[0] - V[0] * T[2], V[0] * T[1] - V[1] * T[0]};
            double sx0, sy0;
            project(v, P, sx0, sy0);
            double sum = 0.0;
            for (int s = 0; s < prm.slices; ++s) {
                const double phi = kPi * (s + 0.5) / prm.slices, cp = std::cos(phi), sp = std::sin(phi);
                const double dir[3] = {cp * T[0] + sp * B[0], cp * T[1] + sp * B[1], cp * T[2] + sp * B[2]};
                // The slice's screen direction: where a small step along dir projects.
                const double e = 1e-3 * v.depth[i], Q[3] = {P[0] + dir[0] * e, P[1] + dir[1] * e, P[2] + dir[2] * e};
                double sx1, sy1;
                project(v, Q, sx1, sy1);
                const double ddl = std::hypot(sx1 - sx0, sy1 - sy0);
                if (ddl < 1e-12) continue;
                const double c = (sx1 - sx0) / ddl, sn = (sy1 - sy0) / ddl;
                const double dv = dot3(dir, V);
                const double od[3] = {dir[0] - dv * V[0], dir[1] - dv * V[1], dir[2] - dv * V[2]};
                double ax[3] = {od[1] * V[2] - od[2] * V[1], od[2] * V[0] - od[0] * V[2], od[0] * V[1] - od[1] * V[0]};
                const double al = std::sqrt(dot3(ax, ax));
                ax[0] /= al; ax[1] /= al; ax[2] /= al;
                const double na = dot3(N, ax);
                const double pn[3] = {N[0] - ax[0] * na, N[1] - ax[1] * na, N[2] - ax[2] * na};
                const double pnl = std::sqrt(dot3(pn, pn));
                if (pnl < 1e-9) continue;
                const double sgn = dot3(od, pn) >= 0.0 ? 1.0 : -1.0;
                const double cn = std::clamp(dot3(pn, V) / pnl, 0.0, 1.0), nang = sgn * std::acos(cn);
                double hc[2] = {std::cos(nang + kPi / 2), std::cos(nang - kPi / 2)};   // +dir side, -dir side
                for (int side = 0; side < 2; ++side) {
                    const double sd = side == 0 ? 1.0 : -1.0;
                    for (int k = 1; k <= prm.steps; ++k) {
                        const double off = sd * rpx * k / prm.steps;
                        const double fx = x + 0.5 + off * c, fy = y + 0.5 + off * sn;
                        const int sx = int(std::floor(fx)), sy = int(std::floor(fy));
                        if (!v.valid(sx, sy) || (sx == x && sy == y)) continue;
                        // The sample at its exact slice position, at the depth of the pixel it falls in.
                        const double D = sampleDepth(v, fx, fy, sx, sy);
                        const double S[3] = {(fx / v.w * 2.0 - 1.0) * D * v.tanHalf * v.aspect, (1.0 - fy / v.h * 2.0) * D * v.tanHalf, -D};
                        const double d[3] = {S[0] - P[0], S[1] - P[1], S[2] - P[2]}, dl = std::sqrt(dot3(d, d));
                        if (dl < 1e-9 || dl > prm.radius) continue;
                        hc[side] = std::max(hc[side], dot3(d, V) / dl);
                    }
                }
                const double h1 = nang + std::min(std::acos(std::clamp(hc[0], -1.0, 1.0)) - nang, kPi / 2);
                const double h0 = nang + std::max(-std::acos(std::clamp(hc[1], -1.0, 1.0)) - nang, -kPi / 2);
                const double sn2 = std::sin(nang);
                const double a0 = (cn + 2.0 * h0 * sn2 - std::cos(2.0 * h0 - nang)) / 4.0;
                const double a1 = (cn + 2.0 * h1 * sn2 - std::cos(2.0 * h1 - nang)) / 4.0;
                sum += pnl * (a0 + a1);
            }
            out[i] = sum / prm.slices;
        }
    });
    return out;
}

std::vector<double> rayAo(const Scene& s, const GBuffer& g, double radius, int sq) {
    std::vector<Tri> tris;
    for (const Mesh& m : s.meshes) for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
        tris.push_back({m.positions[std::size_t(m.indices[i])], m.positions[std::size_t(m.indices[i + 1])], m.positions[std::size_t(m.indices[i + 2])]});
    Bvh bvh; bvh.build(tris);
    const int w = g.depth.w, h = g.depth.h;
    std::vector<double> out(std::size_t(w) * h, 1.0);
    parallelRows(h, threads(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            if (!g.mask.at(x, y)) continue;
            const Vec3 n = normalize(g.normal.at(x, y)), o = g.position.at(x, y) + n * 1e-3f;
            const Vec3 t = normalize(std::fabs(n.x) > 0.9f ? cross(n, Vec3{0, 1, 0}) : cross(n, Vec3{1, 0, 0})), b = cross(n, t);
            int open = 0;
            for (int i = 0; i < sq; ++i) for (int j = 0; j < sq; ++j) {
                const double u1 = (i + 0.5) / sq, u2 = (j + 0.5) / sq, r = std::sqrt(u1), ph = 2.0 * kPi * u2;
                const float lx = float(r * std::cos(ph)), ly = float(r * std::sin(ph)), lz = float(std::sqrt(std::max(0.0, 1.0 - u1)));
                const Vec3 d = t * lx + b * ly + n * lz;
                open += !bvh.occluded({o, d}, float(radius));
            }
            out[std::size_t(y) * w + x] = double(open) / (sq * sq);
        }
    });
    return out;
}

}  // namespace raw::post
