// P23's focus director: see raw/renderer/focus_director.hpp.
#include "raw/renderer/focus_director.hpp"
#include <algorithm>
#include <cmath>
namespace raw::focus {
namespace {
constexpr double kPi = 3.141592653589793;
double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
D3 sub(const D3& a, const D3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
D3 norm(const D3& a) { const double l = std::sqrt(dot(a, a)); return {a[0] / l, a[1] / l, a[2] / l}; }
}  // namespace

CharacterParams characterParams(Character c) {
    CharacterParams p;
    if (c == Character::Accuser) { p.omega = 14; p.idle = 3; p.dwell = 1.5; p.eventHold = 1.2; p.eventMin = 0.3; p.scan = true; }
    else if (c == Character::Child) { p.omega = 10; p.idle = 2.5; p.dwell = 1.2; p.eventHold = 1.5; p.eventMin = 0.2; p.jitterD = 0.08; p.jitterHz = 0.4; }
    return p;
}

void Director::Spring::retarget(double t, double w) {
    target = t;
    // Keep only velocity toward the new target, at most w |e|: the step is then monotone.
    const double e = x - t;
    if (v * e >= 0) v = 0;
    else if (std::abs(v) > w * std::abs(e)) v = -w * e;
}

void Director::Spring::advance(double dt, double w, double zeta) {
    const double e = x - target;
    if (zeta >= 1) {
        const double k = std::exp(-w * dt), q = v + w * e;
        x = target + (e + q * dt) * k;
        v = (v - w * q * dt) * k;
        return;
    }
    const double wd = w * std::sqrt(1 - zeta * zeta), k = std::exp(-zeta * w * dt), c = std::cos(wd * dt), s = std::sin(wd * dt);
    x = target + k * (e * c + (v + zeta * w * e) / wd * s);
    v = k * (v * c - (zeta * w * v + w * w * e) / wd * s);
}

Director::Director(Character c, const art::Lens& base, const art::View& view) : cp_(characterParams(c)), base_(base), view_(view) {
    if (c == Character::Child) base_.fNumber *= 0.7;   // the child's narrow depth of field
}

double Director::axialOf(const D3& p) const {
    const D3 f = norm(sub(view_.target, view_.eye));
    return std::max(1e-3, dot(sub(p, view_.eye), f));
}

void Director::aim(const std::string& who, const D3& p) {
    const double t = 1.0 / axialOf(p);
    if (who != s_.target || std::abs(t - sD_.target) > 1e-12) sD_.retarget(t, cp_.omega);
    s_.target = who;
    s_.targetPoint = p;
}

const State& Director::step(const Inputs& in, double dt) {
    time_ += dt;
    const bool locked = !lockOff && (in.intent == Intent::Talk || in.intent == Intent::Fight || in.intent == Intent::Examine);
    const bool calm = in.reducedMotion;
    if (first_ || in.intent != last_) { mode_ = 0; idleT_ = 0; next_ = 0; }
    D3 subject = in.player;
    std::string who = "player";
    if (in.intent == Intent::Talk) { subject = in.speaker; who = "speaker"; }
    else if (in.intent == Intent::Examine) { subject = in.prop; who = "prop"; }
    else if (in.intent == Intent::Fight && !in.fighters.empty()) {
        subject = {0, 0, 0};
        for (const D3& f : in.fighters) for (int k = 0; k < 3; ++k) subject[std::size_t(k)] += f[std::size_t(k)] / double(in.fighters.size());
        who = "fight";
    }
    const bool wander = !calm && !locked && (in.intent == Intent::Walk || in.intent == Intent::Dream || lockOff);
    // world events: the most salient one this step, if it clears the character's threshold
    const WorldEvent* ev = nullptr;
    if (wander) for (const WorldEvent& e : in.events) if (e.salience >= cp_.eventMin && (!ev || e.salience > ev->salience)) ev = &e;
    if (ev) { mode_ = 2; eventT_ = cp_.eventHold; evPoint_ = ev->p; evKind_ = ev->kind; }
    else if (mode_ == 2) { eventT_ -= dt; if (eventT_ <= 0 || !wander) { mode_ = 0; idleT_ = 0; } }
    // idle drift to a living point and back
    if (!wander || in.playerMoving) { if (mode_ == 1) mode_ = 0; idleT_ = 0; }
    else if (mode_ == 0 && !in.living.empty()) {
        idleT_ += dt;
        if (idleT_ >= cp_.idle) { mode_ = 1; dwellT_ = cp_.dwell; next_ %= in.living.size(); }
    } else if (mode_ == 1) {
        dwellT_ -= dt;
        if (dwellT_ <= 0) {
            ++next_;
            if (cp_.scan && next_ < in.living.size()) dwellT_ = cp_.dwell;
            else { mode_ = 0; idleT_ = 0; if (cp_.scan) next_ = 0; }
        }
    }
    if (mode_ == 1) aim(in.living[next_ % in.living.size()].id, in.living[next_ % in.living.size()].p);
    else if (mode_ == 2) aim("event:" + evKind_, evPoint_);
    else aim(who, subject);
    // the plane's tilt and the f-number
    sT_.target = in.intent == Intent::Dream ? base_.tiltDeg - 45 : base_.tiltDeg;
    double N = base_.fNumber;
    if (in.intent == Intent::Fight && !in.fighters.empty()) {
        art::Lens l = base_;
        const double ax = 1.0 / sD_.target;
        const D3 f = norm(sub(view_.target, view_.eye)), lat = sub(sub(subject, view_.eye), {f[0] * axialOf(subject), f[1] * axialOf(subject), f[2] * axialOf(subject)});
        l.focus = {view_.eye[0] + lat[0] + f[0] * ax, view_.eye[1] + lat[1] + f[1] * ax, view_.eye[2] + lat[2] + f[2] * ax};
        l.cMax1080 = 1e9;
        const art::LensGeom g = art::resolveLens(l, view_);
        double worst = 0;
        for (const D3& p : in.fighters) worst = std::max(worst, art::cocAtPoint(g, p));
        N = base_.fNumber * std::max(1.0, worst / 0.9);
    }
    sN_.target = std::log(std::min(N, 64.0));
    if (first_ || calm) { sD_.x = sD_.target; sD_.v = 0; sT_.x = sT_.target; sT_.v = 0; sN_.x = sN_.target; sN_.v = 0; }
    else { sD_.advance(dt, cp_.omega, damping); sT_.advance(dt, cp_.omega, damping); sN_.advance(dt, cp_.omega, damping); }
    // the child's capped jitter, only where the attention is free
    jitterNow = 0;
    if (cp_.jitterD > 0 && wander) jitterNow = cp_.jitterD * sD_.x * (0.7 * std::sin(2 * kPi * cp_.jitterHz * time_) + 0.3 * std::sin(2 * kPi * 0.23 * time_ + 1.0));
    s_.dioptre = sD_.x + jitterNow;
    s_.rack = sD_.x;
    s_.tiltDeg = sT_.x;
    s_.fNumber = std::exp(sN_.x);
    s_.racking = std::abs(sD_.x - sD_.target) > 1e-6 * std::abs(sD_.target);
    const D3 f = norm(sub(view_.target, view_.eye));
    const double axP = axialOf(s_.targetPoint), ax = 1.0 / s_.dioptre;
    const D3 lat = sub(sub(s_.targetPoint, view_.eye), {f[0] * axP, f[1] * axP, f[2] * axP});
    s_.focus = {view_.eye[0] + lat[0] + f[0] * ax, view_.eye[1] + lat[1] + f[1] * ax, view_.eye[2] + lat[2] + f[2] * ax};
    first_ = false;
    last_ = in.intent;
    return s_;
}

art::Lens Director::lens() const {
    art::Lens l = base_;
    l.focus = s_.focus; l.tiltDeg = s_.tiltDeg; l.fNumber = s_.fNumber;
    return l;
}
art::LensGeom Director::geom() const { return art::resolveLens(lens(), view_); }

}  // namespace raw::focus
