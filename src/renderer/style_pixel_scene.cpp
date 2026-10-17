// The pixel-art diorama and its styling: see raw/renderer/style_pixel.hpp. Operations are in the
// order of web/shaders/pixel/scene.mjs and probe.mjs (stylise) and reference/contracts.mjs (OKLab).
#include "raw/renderer/style_pixel.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
double box(double px, double py, double pz, double cx, double cy, double cz, double bx, double by, double bz) {
    const double qx = std::abs(px - cx) - bx, qy = std::abs(py - cy) - by, qz = std::abs(pz - cz) - bz;
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0), std::max(qz, 0.0)) + std::min(std::max({qx, qy, qz}), 0.0);
}
double sph(double px, double py, double pz, double cx, double cy, double cz, double r) { return std::hypot(px - cx, py - cy, pz - cz) - r; }
double cyl(double px, double py, double pz, double cx, double cz, double y0, double y1, double r) {
    return std::max(std::hypot(px - cx, pz - cz) - r, std::abs(py - (y0 + y1) / 2) - (y1 - y0) / 2);
}
constexpr double kAlbedo[13][3] = {{0.17, 0.32, 0.1}, {0.08, 0.2, 0.32}, {0.62, 0.5, 0.36}, {0.45, 0.13, 0.09}, {0.28, 0.16, 0.08}, {1, 0.8, 0.4},
                                   {0.25, 0.15, 0.08}, {0.1, 0.3, 0.09}, {0.15, 0.22, 0.45}, {0.75, 0.55, 0.42}, {0.1, 0.1, 0.1}, {1, 0.75, 0.35}, {0.4, 0.4, 0.42}};
PV3 sunDir() { const double l = std::hypot(-0.45, 0.8, 0.4); return {-0.45 / l, 0.8 / l, 0.4 / l}; }
constexpr double kLantern[3] = {0.2, 0.98, 1.75};
PV3 toOklab(double r, double g, double b) {
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s, 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
PV3 fromOklab(double L, double a, double b) {
    const double l_ = L + 0.3963377774 * a + 0.2158037573 * b, m_ = L - 0.1055613458 * a - 0.0638541728 * b, s_ = L - 0.0894841775 * a - 1.2914855480 * b;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s, -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}
}  // namespace

std::array<double, 3> pixelSceneMap(double px, double py, double pz) {
    double d = py, m = 0, o = 0;
    const double pond = std::max(std::hypot(px - 2.2, (pz - 1.6) * 1.4) - 0.9, std::abs(py + 0.02) - 0.03);
    if (pond < d) { d = pond; m = 1; o = 1; }
    const double walls = box(px, py, pz, -1, 0.8, -0.6, 1.2, 0.8, 0.9);
    if (walls < d) { d = walls; m = 2; o = 2; }
    const double rx = (px + 1) * 0.7071 + (py - 1.6) * 0.7071, ry = -(px + 1) * 0.7071 + (py - 1.6) * 0.7071;
    const double roof = std::max(box(rx, ry, pz, 0, 0, -0.6, 0.95, 0.95, 1.05), -py + 1.6);
    if (roof < d) { d = roof; m = 3; o = 3; }
    const double door = box(px, py, pz, -0.5, 0.45, 0.32, 0.22, 0.45, 0.04);
    if (door < d) { d = door; m = 4; o = 4; }
    const double win = box(px, py, pz, -1.45, 0.95, 0.32, 0.25, 0.2, 0.03);
    if (win < d) { d = win; m = 5; o = 5; }
    const double trunk = cyl(px, py, pz, 1.9, -1.2, 0, 1.1, 0.13);
    if (trunk < d) { d = trunk; m = 6; o = 6; }
    const double leaves = std::min(sph(px, py, pz, 1.9, 1.5, -1.2, 0.62), sph(px, py, pz, 2.25, 1.15, -0.95, 0.42));
    if (leaves < d) { d = leaves; m = 7; o = 7; }
    const double body = cyl(px, py, pz, 0.6, 1.0, 0, 0.62, 0.16);
    if (body < d) { d = body; m = 8; o = 8; }
    const double head = sph(px, py, pz, 0.6, 0.78, 1.0, 0.13);
    if (head < d) { d = head; m = 9; o = 9; }
    const double post = cyl(px, py, pz, 0.2, 1.75, 0, 0.9, 0.035);
    if (post < d) { d = post; m = 10; o = 10; }
    const double lamp = box(px, py, pz, 0.2, 0.98, 1.75, 0.08, 0.09, 0.08);
    if (lamp < d) { d = lamp; m = 11; o = 11; }
    const double rock = sph(px, py, pz, -2.3, 0.15, 1.4, 0.38) + 0.04 * std::sin(px * 9) * std::sin(pz * 7);
    if (rock < d) { d = rock; m = 12; o = 12; }
    return {d, m, o};
}

PV3 pixelSceneNormal(const PV3& p) {
    const double e = 1e-3;
    auto f = [](double x, double y, double z) { return pixelSceneMap(x, y, z)[0]; };
    const double nx = f(p[0] + e, p[1], p[2]) - f(p[0] - e, p[1], p[2]), ny = f(p[0], p[1] + e, p[2]) - f(p[0], p[1] - e, p[2]), nz = f(p[0], p[1], p[2] + e) - f(p[0], p[1], p[2] - e);
    const double l = std::hypot(nx, ny, nz);
    return {nx / l, ny / l, nz / l};
}
SceneHit pixelMarch(const PV3& o, const PV3& r, double tMax) {
    double t = 0;
    for (int i = 0; i < 160 && t < tMax; ++i) {
        const auto s = pixelSceneMap(o[0] + r[0] * t, o[1] + r[1] * t, o[2] + r[2] * t);
        if (s[0] < 1e-3 * (1 + t)) return {true, t, int(s[1]), int(s[2])};
        t += s[0] * 0.9;
    }
    return {};
}
PV3 pixelSky(const PV3& r) { const double k = std::max(0.0, r[1]); return {0.85 - 0.6 * k, 0.82 - 0.4 * k, 0.78 + 0.1 * k}; }
PV3 pixelShadePoint(const PV3& p, const PV3& n, int m) {
    if (m == 5 || m == 11) { const double e = m == 5 ? 1.6 : 4; return {kAlbedo[m][0] * e, kAlbedo[m][1] * e, kAlbedo[m][2] * e}; }
    PV3 alb{kAlbedo[m][0], kAlbedo[m][1], kAlbedo[m][2]};
    if (m == 0) { const double k = ((int(std::floor(p[0] * 2)) + int(std::floor(p[2] * 2))) & 1) ? 1 : 0.86; for (double& a : alb) a = a * k; }
    const PV3 S = sunDir();
    const SceneHit sh = pixelMarch({p[0] + S[0] * 0.02, p[1] + S[1] * 0.02, p[2] + S[2] * 0.02}, S, 12);
    const double sun = std::max(0.0, n[0] * S[0] + n[1] * S[1] + n[2] * S[2]) * (sh.hit ? 0 : 1) * 2.6;
    const double skyF = (0.55 + 0.45 * n[1]) * 0.55;
    const double lx = kLantern[0] - p[0], ly = kLantern[1] - p[1], lz = kLantern[2] - p[2], ld = std::hypot(lx, ly, lz);
    const double lan = std::max(0.0, (n[0] * lx + n[1] * ly + n[2] * lz) / ld) * 1.2 / (ld * ld + 0.2);
    constexpr double ks[3] = {1, 0.93, 0.8}, kk[3] = {0.6, 0.75, 1}, kl[3] = {1, 0.7, 0.35};
    PV3 out{};
    for (int k = 0; k < 3; ++k) out[std::size_t(k)] = alb[std::size_t(k)] * (sun * ks[k] + skyF * kk[k] + lan * kl[k]);
    return out;
}
PV3 pixelStylise(const PV3& rgb, double bands, double dL) {
    const PV3 lab = toOklab(std::max(rgb[0], 0.0), std::max(rgb[1], 0.0), std::max(rgb[2], 0.0));
    const double Lq = std::min(1.0, std::max(0.0, std::floor((lab[0] + dL) * bands + 0.5) / bands));
    PV3 o = fromOklab(Lq, lab[1], lab[2]);
    for (double& v : o) v = std::max(0.0, v);
    return o;
}

}  // namespace raw::style
