// The impact preview bench's drawing: schedule cues to frames. See raw/tools/impact_cmd.hpp.
#include "raw/tools/impact_cmd.hpp"
#include "raw/renderer/impact.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace raw {
namespace {

using game::action::Channel;
using game::action::Cue;
using impact::Rgb;

constexpr double PI = 3.14159265358979323846, DEG = 180 / PI, BLADE_Y = 1.1;
const Rgb GROUND{22, 24, 28}, GRID{34, 37, 42}, LAMB{232, 196, 140}, FOE{150, 172, 204}, STEEL{222, 226, 232}, SMEAR{200, 212, 228},
          TRAIL{120, 220, 255}, VOLUME{176, 128, 255}, BURST{255, 214, 120}, MARK{246, 246, 246};
constexpr int TRAIL_LIFE = 8, BURST_LIFE = 10, MARK_LIFE = 18;

struct Cam {
    double cx = 0, cz = 0, s = 1, w = 0, h = 0, ox = 0, oy = 0;
    void at(double x, double z, double y, double& sx, double& sy) const { sx = w / 2 + (x - cx) * s + ox; sy = h * 0.62 + (z - cz) * s * 0.82 - y * s * 0.9 + oy; }
};

void world(const Cue& pose, double r, double yaw_deg, double& x, double& z) {
    const double a = pose.v[4] + yaw_deg / DEG; x = pose.v[2] + r * std::cos(a); z = pose.v[3] + r * std::sin(a);
}

void body(impact::Canvas& cv, const Cam& c, const Cue& p, Rgb col) {
    double x, y, hx, hy, fx, fy;
    c.at(p.v[2], p.v[3], 0, x, y); c.at(p.v[2], p.v[3], 1.45, hx, hy); c.at(p.v[2], p.v[3], 1.68, fx, fy);
    impact::draw_disc(cv, x, y, 0.22 * c.s, {12, 13, 15}, 0.55);
    impact::draw_segment(cv, x, y - 0.18 * c.s, hx, hy, 0.09 * c.s, col, 1);
    impact::draw_disc(cv, fx, fy, 0.12 * c.s, col, 1);
    double nx, nz; world(p, 0.3, 0, nx, nz);   // a nose: which way the body faces
    double sx, sy; c.at(nx, nz, 1.5, sx, sy);
    impact::draw_segment(cv, hx, hy, sx, sy, 0.025 * c.s, col, 0.9);
}

}  // namespace

ImpactFrames render_impact(const game::action::Schedule& s, const ImpactView& v) {
    std::map<int, std::vector<const Cue*>> by;
    for (const Cue& c : s.cues) by[c.tick].push_back(&c);
    ImpactFrames out; out.w = v.w; out.h = v.h; out.burst_first.assign(s.contacts.size(), -1);
    Cam cam; cam.w = v.w; cam.h = v.h; cam.s = v.w / v.metres;
    { int n = 0; for (const Cue* c : by[v.from]) if (c->ch == Channel::Pose) { cam.cx += c->v[2]; cam.cz += c->v[3]; n++; } if (n) { cam.cx /= n; cam.cz /= n; } }
    impact::ShakeCam shake; shake.view_h = v.h;
    for (int i = 0; i < v.frames; i++) {
        const int k = v.from + i;
        bool tell = false;
        for (const Cue* c : by[k]) { if (c->ch == Channel::Shake) shake.blow(c->v[0], c->kind == "catch", c->v[2], c->v[3]); if (c->ch == Channel::Pose && c->tell) tell = true; }
        shake.tells(tell);
        const impact::Offset off = shake.tick(1.0 / 60);
        cam.ox = off.dx; cam.oy = off.dy; out.shake_px.push_back(std::hypot(off.dx, off.dy));
        impact::Canvas cv(v.w, v.h, GROUND), trail(v.w, v.h, {0, 0, 0}), burst(v.w, v.h, {0, 0, 0});
        for (int g = -12; g <= 12; g++) {   // a floor grid, metres apart, so a shake reads against the ground
            double a, b, c2, d; cam.at(cam.cx + g, cam.cz - 6, 0, a, b); cam.at(cam.cx + g, cam.cz + 6, 0, c2, d); impact::draw_segment(cv, a, b, c2, d, 0.6, GRID, 1);
            cam.at(cam.cx - 12, cam.cz + g, 0, a, b); cam.at(cam.cx + 12, cam.cz + g, 0, c2, d); impact::draw_segment(cv, a, b, c2, d, 0.6, GRID, 1);
        }
        std::map<int, const Cue*> pose;
        for (const Cue* c : by[k]) if (c->ch == Channel::Pose) pose[c->actor] = c;
        for (const Cue* c : by[k]) if (c->ch == Channel::Smear && pose.count(c->actor)) {   // the swing's travel
            const Cue& p = *pose[c->actor];
            std::vector<std::array<double, 2>> poly;
            for (int j = 0; j <= 12; j++) { double x, z, sx, sy; world(p, c->v[3], c->v[0] + (c->v[1] - c->v[0]) * j / 12, x, z); cam.at(x, z, BLADE_Y, sx, sy); poly.push_back({sx, sy}); }
            for (int j = 12; j >= 0; j--) { double x, z, sx, sy; world(p, c->v[2], c->v[0] + (c->v[1] - c->v[0]) * j / 12, x, z); cam.at(x, z, BLADE_Y, sx, sy); poly.push_back({sx, sy}); }
            impact::fill_polygon(cv, poly, SMEAR, 0.5);
        }
        for (auto& [a, p] : pose) body(cv, cam, *p, a == 0 ? LAMB : FOE);
        for (auto& [a, p] : pose) if (p->v[7] > 0) {   // the blade at the pose's clip time, out to the capsule's tip
            double x0, z0, x1, z1, s0, t0, s1, t1;
            world(*p, p->v[6], p->v[5], x0, z0); world(*p, p->v[7], p->v[5], x1, z1);
            cam.at(x0, z0, BLADE_Y, s0, t0); cam.at(x1, z1, BLADE_Y, s1, t1);
            impact::draw_segment(cv, s0, t0, s1, t1, 0.03 * cam.s, STEEL, 1);
        }
        for (auto& [a, p] : pose) {   // the trail: this actor's ribbon over its last active ticks, only while it is active
            bool live = false; for (const Cue* c : by[k]) if (c->ch == Channel::Trail && c->actor == a) live = true;
            if (!live) continue;
            for (int age = TRAIL_LIFE - 1; age >= 0; age--)
                for (const Cue* c : by[k - age]) if (c->ch == Channel::Trail && c->actor == a) {
                    double s0, t0, s1, t1; cam.at(c->v[0], c->v[1], BLADE_Y, s0, t0); cam.at(c->v[2], c->v[3], BLADE_Y, s1, t1);
                    impact::draw_ribbon(cv, s0, t0, s1, t1, age, TRAIL_LIFE, TRAIL); impact::draw_ribbon(trail, s0, t0, s1, t1, age, TRAIL_LIFE, {255, 255, 255});
                }
        }
        if (v.overlays) for (const Cue* c : by[k]) if (c->ch == Channel::HitVolume) {
            double s0, t0, s1, t1; cam.at(c->v[0], c->v[1], BLADE_Y, s0, t0); cam.at(c->v[2], c->v[3], BLADE_Y, s1, t1);
            impact::draw_segment(cv, s0, t0, s1, t1, 0.05 * cam.s, VOLUME, 0.35);
        }
        for (int age = 0; age < MARK_LIFE; age++) for (const Cue* c : by[k - age]) {
            double sx, sy; cam.at(c->v[0], c->v[1], BLADE_Y, sx, sy);
            if (c->ch == Channel::Effect && age < BURST_LIFE) {
                impact::draw_burst(cv, sx, sy, c->v[2] * 0.32 * cam.s, 9, std::fmod(c->v[3] * 0.618, 1.0) * 2 * PI, age, BURST_LIFE, BURST);
                impact::draw_burst(burst, sx, sy, c->v[2] * 0.32 * cam.s, 9, std::fmod(c->v[3] * 0.618, 1.0) * 2 * PI, age, BURST_LIFE, {255, 255, 255});
                if (c->contact >= 0 && out.burst_first[(size_t)c->contact] < 0) out.burst_first[(size_t)c->contact] = i;
            }
            if (c->ch == Channel::Hitmarker) impact::draw_hitmarker(cv, sx, sy, 0.22 * cam.s, age, MARK_LIFE, MARK);
        }
        int tp = 0, bp = 0;
        for (size_t p = 0; p < trail.rgb.size(); p += 3) { tp += trail.rgb[p] > 0; bp += burst.rgb[p] > 0; }
        out.trail_px.push_back(tp); out.burst_px.push_back(bp);
        out.rgb.push_back(std::move(cv.rgb));
    }
    return out;
}

}  // namespace raw
