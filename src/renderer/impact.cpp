// Impact presentation on the CPU: see raw/renderer/impact.hpp.
#include "raw/renderer/impact.hpp"
#include <algorithm>
#include <cmath>

namespace raw::impact {
namespace {

constexpr double PI = 3.14159265358979323846, SHAKE_MAX = 6, TELL_CAP = 3;
constexpr double SK = 300, SC = 30, SDT = 0.01;   // fightcam.mjs SPRING
// fightcam.mjs KEYS: short, medium, catch
const std::vector<std::array<double, 2>> KEYS[3] = {
    {{0, 0}, {3, 1}, {7.5, -0.4}, {12, 0}},
    {{0, 0}, {3, 1}, {8, -0.4}, {11.5, 0.12}, {15, 0}},
    {{0, 0}, {3, 1}, {10, -0.45}, {20, 0.25}, {30, -0.08}, {40, 0}},
};

bool inside(const std::vector<std::array<double, 2>>& p, double x, double y) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if ((p[i][1] > y) != (p[j][1] > y) && x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]) in = !in;
    return in;
}

}  // namespace

bool keyed(int keys, double f, double& out) {
    const auto& k = KEYS[keys];
    if (f >= k.back()[0]) return false;
    for (size_t i = 1; i < k.size(); i++)
        if (f <= k[i][0]) {
            const double f0 = k[i - 1][0], v0 = k[i - 1][1], f1 = k[i][0], v1 = k[i][1], t = (f - f0) / (f1 - f0), s = t * t * (3 - 2 * t);
            out = v0 + (v1 - v0) * s;
            return true;
        }
    out = 0;
    return true;
}

void ShakeCam::blow(double amp_in, bool is_catch, double dx, double dy) {
    const double amp = std::min(1.0, amp_in != 0 ? amp_in : 0.2), a = std::pow(amp, 0.7);
    double L = std::hypot(dx, dy); if (L == 0) L = 1;
    shake_ = {}; shake_.amp = a * SHAKE_MAX; shake_.d[0] = dx / L; shake_.d[1] = dy / L;
    shake_.keys = is_catch ? 2 : amp >= 0.5 ? 1 : 0;
    has_ = true;
    v_[0] += shake_.d[0] * a * SHAKE_MAX * kick; v_[1] += shake_.d[1] * a * SHAKE_MAX * kick;
}

Offset ShakeCam::tick(double dt) {
    double dx = 0, dy = 0;
    const double g = view_h / 1080;
    if (has_) {
        shake_.f += dt * 60;
        double k = 0;
        if (!keyed(shake_.keys, shake_.f / key_stretch, k)) has_ = false;
        else { const double v = shake_.amp * g * k; dx = shake_.d[0] * v; dy = shake_.d[1] * v; }
    }
    for (acc_ += dt; acc_ >= SDT; acc_ -= SDT)
        for (int i = 0; i < 2; i++) { const double a = -SK * x_[i] - SC * v_[i]; v_[i] += a * SDT; x_[i] += v_[i] * SDT; }
    if (std::hypot(x_[0], x_[1]) > 0.05 || std::hypot(v_[0], v_[1]) > 0.5) { dx += x_[0] * g; dy += x_[1] * g; }
    else { x_[0] = x_[1] = v_[0] = v_[1] = 0; }
    const double lim = (tell_ && tell_cap ? TELL_CAP : SHAKE_MAX) * g, m = std::hypot(dx, dy);
    if (m > lim) { dx *= lim / m; dy *= lim / m; }
    return {dx, dy};
}

Canvas::Canvas(int W, int H, Rgb ground) : w(W), h(H), rgb((size_t)W * H * 3) {
    for (size_t i = 0; i < rgb.size(); i += 3) { rgb[i] = ground[0]; rgb[i + 1] = ground[1]; rgb[i + 2] = ground[2]; }
}

void Canvas::blend(int x, int y, Rgb c, double a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
    a = std::min(1.0, a);
    std::uint8_t* p = &rgb[((size_t)y * w + x) * 3];
    for (int i = 0; i < 3; i++) p[i] = (std::uint8_t)std::lround(p[i] + (c[(size_t)i] - p[i]) * a);
}

void draw_segment(Canvas& cv, double x0, double y0, double x1, double y1, double hw, Rgb c, double alpha) {
    const int xa = (int)std::floor(std::min(x0, x1) - hw - 1), xb = (int)std::ceil(std::max(x0, x1) + hw + 1);
    const int ya = (int)std::floor(std::min(y0, y1) - hw - 1), yb = (int)std::ceil(std::max(y0, y1) + hw + 1);
    const double ux = x1 - x0, uy = y1 - y0, l2 = ux * ux + uy * uy;
    for (int y = std::max(0, ya); y <= std::min(cv.h - 1, yb); y++)
        for (int x = std::max(0, xa); x <= std::min(cv.w - 1, xb); x++) {
            const double px = x + 0.5 - x0, py = y + 0.5 - y0, t = l2 > 0 ? std::clamp((px * ux + py * uy) / l2, 0.0, 1.0) : 0.0;
            const double d = std::hypot(px - t * ux, py - t * uy);
            cv.blend(x, y, c, alpha * std::clamp(hw + 0.5 - d, 0.0, 1.0));
        }
}

void draw_disc(Canvas& cv, double x, double y, double r, Rgb c, double alpha) { draw_segment(cv, x, y, x, y, r, c, alpha); }

void fill_polygon(Canvas& cv, const std::vector<std::array<double, 2>>& p, Rgb c, double alpha) {
    if (p.size() < 3) return;
    double xa = 1e18, xb = -1e18, ya = 1e18, yb = -1e18;
    for (auto& q : p) { xa = std::min(xa, q[0]); xb = std::max(xb, q[0]); ya = std::min(ya, q[1]); yb = std::max(yb, q[1]); }
    for (int y = std::max(0, (int)std::floor(ya)); y <= std::min(cv.h - 1, (int)std::ceil(yb)); y++)
        for (int x = std::max(0, (int)std::floor(xa)); x <= std::min(cv.w - 1, (int)std::ceil(xb)); x++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) n += inside(p, x + (sx + 0.5) / 4, y + (sy + 0.5) / 4);
            if (n) cv.blend(x, y, c, alpha * n / 16.0);
        }
}

void draw_ribbon(Canvas& cv, double x0, double y0, double x1, double y1, int age, int life, Rgb c) {
    const double k = 1 - (double)age / life;
    if (k <= 0) return;
    draw_segment(cv, x0, y0, x1, y1, 1.5 + 7 * k, c, 0.25 + 0.65 * k);
}

void draw_smear(Canvas& cv, double cx, double cy, double a0, double a1, double r0, double r1, Rgb c, double alpha) {
    std::vector<std::array<double, 2>> p;
    const int n = 16;
    for (int i = 0; i <= n; i++) { const double a = a0 + (a1 - a0) * i / n; p.push_back({cx + r1 * std::cos(a), cy + r1 * std::sin(a)}); }
    for (int i = n; i >= 0; i--) { const double a = a0 + (a1 - a0) * i / n; p.push_back({cx + r0 * std::cos(a), cy + r0 * std::sin(a)}); }
    fill_polygon(cv, p, c, alpha);
}

void draw_burst(Canvas& cv, double cx, double cy, double radius, int teeth, double spin, int age, int life, Rgb c) {
    const double t = (double)age / life;
    if (t >= 1) return;
    const double out = radius * (0.55 + 0.75 * t), in = out * (0.42 + 0.2 * t), step = PI / teeth, alpha = t < 0.5 ? 1.0 : 1 - (t - 0.5) * 2;
    std::vector<std::array<double, 2>> p;
    for (int i = 0; i < teeth; i++) {
        const double a = spin + 2 * i * step;   // a sawtooth: the tip leans forward, the back edge drops straight
        p.push_back({cx + out * std::cos(a + step * 0.7), cy + out * std::sin(a + step * 0.7)});
        p.push_back({cx + in * std::cos(a + step), cy + in * std::sin(a + step)});
        p.push_back({cx + in * std::cos(a + 2 * step), cy + in * std::sin(a + 2 * step)});
    }
    fill_polygon(cv, p, c, alpha);
}

void draw_hitmarker(Canvas& cv, double cx, double cy, double size, int age, int life, Rgb c) {
    const double t = (double)age / life;
    if (t >= 1) return;
    const double a = t < 0.6 ? 1.0 : 1 - (t - 0.6) / 0.4, r0 = size * (0.35 + 0.35 * t), r1 = size * (0.75 + 0.35 * t);
    for (int i = 0; i < 4; i++) {
        const double ang = PI / 4 + i * PI / 2, c0 = std::cos(ang), s0 = std::sin(ang);
        draw_segment(cv, cx + r0 * c0, cy + r0 * s0, cx + r1 * c0, cy + r1 * s0, std::max(1.0, size * 0.06), c, a);
    }
}

}  // namespace raw::impact
