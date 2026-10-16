// Paint-over projection, float64 reference: see raw/renderer/style_paintover.hpp. Operations are
// in the order of web/shaders/paintover/camera.mjs and projector.mjs.
#include "raw/renderer/style_paintover.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
namespace raw::style {
namespace {
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 norm(const V3& a) { const double l = std::hypot(a[0], a[1], a[2]); return {a[0] / l, a[1] / l, a[2] / l}; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double window2(double a) { const double t = std::max(0.0, 1 - a * a); return t * t; }
double halfDist(double x) { return std::abs(x - std::floor(x) - 0.5); }   // distance from the rounding edge
}  // namespace

PaintCamera makePaintCamera(bool ortho, V3 pos, V3 fwd, double scale, double fovY) {
    PaintCamera c;
    c.ortho = ortho; c.pos = pos; c.fwd = norm(fwd);
    c.right = norm(cross(c.fwd, {0, 1, 0}));
    c.up = norm(cross(c.right, c.fwd));
    c.scale = scale; c.fovY = fovY;
    return c;
}
PaintCamera isoCamera(double yawDeg, double elevDeg, V3 target, double dist, double scale, bool ortho, double fovY) {
    const double y = (yawDeg * std::numbers::pi) / 180, e = (elevDeg * std::numbers::pi) / 180;
    const V3 fwd{-std::sin(y) * std::cos(e), -std::sin(e), -std::cos(y) * std::cos(e)};
    return makePaintCamera(ortho, {target[0] - fwd[0] * dist, target[1] - fwd[1] * dist, target[2] - fwd[2] * dist}, fwd, scale, fovY);
}
V3 project(const PaintCamera& c, int w, int h, const V3& P) {
    const V3 r{P[0] - c.pos[0], P[1] - c.pos[1], P[2] - c.pos[2]};
    const double z = dot(r, c.fwd), asp = double(w) / h, a = dot(r, c.right), b = dot(r, c.up);
    double u, v;
    if (c.ortho) { u = a / (c.scale * asp); v = b / c.scale; }
    else { const double t = std::tan(c.fovY / 2); u = a / (z * t * asp); v = b / (z * t); }
    return {((u + 1) / 2) * w - 0.5, ((1 - v) / 2) * h - 0.5, z};
}
V3 toCamera(const PaintCamera& c, const V3& P) {
    if (c.ortho) return {-c.fwd[0], -c.fwd[1], -c.fwd[2]};
    return norm({c.pos[0] - P[0], c.pos[1] - P[1], c.pos[2] - P[2]});
}
double pixelWorld(const PaintCamera& c, int h, double depth) {
    return c.ortho ? (2 * c.scale) / h : (2 * std::tan(c.fovY / 2) * depth) / h;
}

std::vector<double> reaches(const std::vector<const Projector*>& set, const PaintOptions& o) {
    const double tm = (o.thetaMaxDeg * std::numbers::pi) / 180;
    std::vector<double> r(set.size(), tm);
    if (!o.reach || set.size() < 2) return r;
    for (std::size_t k = 0; k < set.size(); ++k) {
        double m = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < set.size(); ++j)
            if (j != k) m = std::min(m, std::acos(std::min(1.0, std::max(-1.0, dot(set[k]->cam.fwd, set[j]->cam.fwd)))));
        r[k] = std::min(tm, std::max(m, (8 * std::numbers::pi) / 180));
    }
    return r;
}

V3 sampleRgb(const Projector& p, double x, double y, double scale) {
    const int w = p.w, h = p.h;
    const double xc = std::min(double(w - 1), std::max(0.0, x)), yc = std::min(double(h - 1), std::max(0.0, y));
    const int x0 = int(std::floor(xc)), y0 = int(std::floor(yc)), x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    const double fx = xc - x0, fy = yc - y0;
    V3 o{};
    for (std::size_t c = 0; c < 3; ++c) {
        const double a = p.rgba[(std::size_t(y0) * w + x0) * 4 + c], b = p.rgba[(std::size_t(y0) * w + x1) * 4 + c];
        const double d = p.rgba[(std::size_t(y1) * w + x0) * 4 + c], e = p.rgba[(std::size_t(y1) * w + x1) * 4 + c];
        o[c] = ((a * (1 - fx) + b * fx) * (1 - fy) + (d * (1 - fx) + e * fx) * fy) / scale;
    }
    return o;
}

std::optional<Coverage> covered(const Projector& pr, const V3& P, const V3& N, const PaintOptions& o, double* marginOut) {
    const V3 p = project(pr.cam, pr.w, pr.h, P);
    double margin = std::min({std::abs(p[0] + 0.5), std::abs(pr.w - 0.5 - p[0]), std::abs(p[1] + 0.5), std::abs(pr.h - 0.5 - p[1])});
    auto done = [&](std::optional<Coverage> c) { if (marginOut) *marginOut = margin; if (c) c->margin = margin; return c; };
    if (!(p[0] >= -0.5 && p[0] <= pr.w - 0.5 && p[1] >= -0.5 && p[1] <= pr.h - 0.5)) return done(std::nullopt);
    const V3 to = toCamera(pr.cam, P);
    const double f = dot(N, to);
    margin = std::min(margin, std::abs(f - o.fMin));
    if (f < o.fMin) return done(std::nullopt);
    // Math.round in the JavaScript reference is floor(x + 0.5)
    const int ix = std::min(pr.w - 1, std::max(0, int(std::floor(p[0] + 0.5)))), iy = std::min(pr.h - 1, std::max(0, int(std::floor(p[1] + 0.5))));
    margin = std::min({margin, halfDist(p[0]), halfDist(p[1])});
    const double dm = pr.depth[std::size_t(iy) * pr.w + ix];
    const double tol = pixelWorld(pr.cam, pr.h, p[2]) * (o.tolBase + o.tolGrazing * (1 - f));
    margin = std::min(margin, std::abs((p[2] - dm) - tol));
    if (p[2] - dm > tol) return done(std::nullopt);
    return done(Coverage{p[0], p[1], f, margin});
}

PaintShade shadePoint(const std::vector<const Projector*>& set, const std::vector<double>& reach, const V3& P, const V3& N,
                      const PaintCamera& view, const PaintOptions& o) {
    const V3 v = toCamera(view, P);
    double wsum = 0, r = 0, g = 0, b = 0;
    PaintShade s;
    for (std::size_t k = 0; k < set.size(); ++k) {
        const Projector& pr = *set[k];
        double m = 0;
        const std::optional<Coverage> c = covered(pr, P, N, o, &m);
        s.margin = std::min(s.margin, m);
        if (!c) continue;
        s.hit++;
        const double ang = std::acos(std::min(1.0, std::max(-1.0, dot(toCamera(pr.cam, P), v))));
        const double w = std::pow(c->f, o.facingPow) * std::max(window2(ang / reach[k]), o.floor);
        const V3 col = sampleRgb(pr, c->px, c->py, o.sampleScale);
        wsum += w; r += w * col[0]; g += w * col[1]; b += w * col[2];
    }
    if (wsum > 0) { s.covered = true; s.rgb = {r / wsum, g / wsum, b / wsum}; }
    return s;
}

}  // namespace raw::style
