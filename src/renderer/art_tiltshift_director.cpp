// P23's director checks d1 to d5 (evidence/tiltshift-bounds.json): a scripted 90 s timeline on the
// town at 60 Hz in the town_map look (the strongest miniature), measured from the director's own
// lens state. Dioptres are the lens's (the model's scale): K / axial distance in metres.
#include "raw/renderer/art_tiltshift_checks.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
constexpr double kDt = 1.0 / 60, kC0 = 1.0, kPi = 3.141592653589793;
D3 at(const TownPerson& p, double y) { return {p.foot[0], y, p.foot[2]}; }
std::vector<focus::Living> living() {
    std::vector<focus::Living> v;
    for (const TownPoint& p : townLiving()) v.push_back({p.p, p.id, p.salience});
    return v;
}
struct Span { double t0, t1; focus::Intent intent; bool moving; };
const Span kTimeline[] = {{0, 20, focus::Intent::Walk, false}, {20, 40, focus::Intent::Talk, false}, {40, 45, focus::Intent::Walk, true},
                          {45, 50, focus::Intent::Walk, false}, {50, 70, focus::Intent::Fight, false}, {70, 78, focus::Intent::Examine, false},
                          {78, 90, focus::Intent::Dream, false}};
struct Ev { double t; D3 p; const char* kind; };
const Ev kEvents[] = {{12, {-7.5, 1.5, -5.0}, "door"}, {25, {-7.5, 1.5, -5.0}, "door"}, {30, {6.0, 0.9, 0.6}, "runner"},
                      {35, {12.0, 0.8, 2.0}, "cart"}, {55, {-7.5, 1.5, -5.0}, "door"}, {62, {12.0, 0.8, 2.0}, "cart"}};
View view1080() { return townView(1920, 1080); }
focus::Director make(focus::Character c) {
    const TiltLook look = tiltLook("town_map");
    return focus::Director(c, lensOf(look, {0, 0.9, 0}, {1, 0, 0}), view1080());
}
double lensDioptre(const View& v, double K, const D3& p) {
    const D3 f{v.target[0] - v.eye[0], v.target[1] - v.eye[1], v.target[2] - v.eye[2]};
    const double l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    return K / (((p[0] - v.eye[0]) * f[0] + (p[1] - v.eye[1]) * f[1] + (p[2] - v.eye[2]) * f[2]) / l);
}
struct RunOut { double overshoot{0}; int lockFails{0}, checked{0}, eventsInLocks{0}; double idleInLocks{0}; };
// The timeline: speaker is person 1; fighters persons 1 to 3 (body and head); the prop the window.
RunOut runTimeline(focus::Director& d, bool calm) {
    RunOut r;
    const auto& P = townPeople();
    const View v = view1080();
    const double K = tiltLook("town_map").K;
    double prevE = 0, prevT = -1;
    for (int i = 0; i < int(90 / kDt); ++i) {
        const double t = i * kDt;
        const Span* sp = &kTimeline[0];
        for (const Span& s : kTimeline) if (t >= s.t0 && t < s.t1) sp = &s;
        focus::Inputs in;
        in.intent = sp->intent; in.playerMoving = sp->moving; in.reducedMotion = calm; in.living = living();
        in.player = at(P[2], 0.9); in.speaker = at(P[1], 1.61); in.prop = {6.6, 2.9, -4.98};
        for (int k = 1; k <= 3; ++k) { in.fighters.push_back(at(P[std::size_t(k)], 0.9)); in.fighters.push_back(at(P[std::size_t(k)], 1.61)); }
        for (const Ev& e : kEvents) if (std::abs(t - e.t) < 0.5 * kDt) { in.events.push_back({e.p, e.kind, 0.9}); }
        const bool lockSpan = sp->intent == focus::Intent::Talk || sp->intent == focus::Intent::Fight;
        if (lockSpan) { r.eventsInLocks += int(in.events.size()); r.idleInLocks += kDt; }
        const focus::State& s = d.step(in, kDt);
        // d1: the rack spring, in lens dioptres, toward an unchanged target
        const double T = lensDioptre(v, K, s.targetPoint), e = K * s.rack - T;
        if (prevT >= 0 && std::abs(T - prevT) < 1e-12 && std::abs(prevE) > 0) {
            if (e * prevE < 0) r.overshoot = std::max(r.overshoot, std::abs(e));
            else if (std::abs(e) > std::abs(prevE) + 1e-12) r.overshoot = std::max(r.overshoot, std::abs(e) - std::abs(prevE));
        }
        prevE = e; prevT = T;
        // d2: from 1.0 s into a talk or fight span
        if (lockSpan && t >= sp->t0 + 1.0) {
            ++r.checked;
            const LensGeom g = d.geom();
            bool ok = true;
            if (sp->intent == focus::Intent::Talk) ok = s.target == "speaker" && cocAtPoint(g, in.speaker) <= kC0 && cocAtPoint(g, at(P[1], 0.9)) <= kC0;
            else { ok = s.target == "fight"; for (const D3& f : in.fighters) ok = ok && cocAtPoint(g, f) <= kC0; }
            r.lockFails += !ok;
        }
    }
    return r;
}
}  // namespace

DirectorReport runDirectorChecks() {
    DirectorReport rep;
    {
        focus::Director d = make(focus::Character::Naturalist);
        const RunOut r = runTimeline(d, false);
        rep.maxOvershoot = r.overshoot; rep.lockFails = r.lockFails; rep.lockFramesChecked = r.checked;
        rep.eventsInLocks = r.eventsInLocks; rep.idleInLocks = r.idleInLocks;
    }
    { focus::Director d = make(focus::Character::Naturalist); d.damping = 0.5; rep.controlOvershoot = runTimeline(d, false).overshoot; }
    { focus::Director d = make(focus::Character::Naturalist); d.lockOff = true; rep.controlLockFails = runTimeline(d, false).lockFails; }
    // d3: a 60 s idle walk, naturalist, and the same under reduced motion
    const auto& P = townPeople();
    for (int calm = 0; calm < 2; ++calm) {
        focus::Director d = make(focus::Character::Naturalist);
        std::string prev = "player";
        double since = 0, dwellEnd = -1;
        for (int i = 0; i < int(60 / kDt); ++i) {
            focus::Inputs in;
            in.player = at(P[2], 0.9); in.living = living(); in.reducedMotion = calm == 1;
            const focus::State& s = d.step(in, kDt);
            if (i > 0 && s.racking && calm) ++rep.calmAnimated;
            if (s.target != prev) {
                if (s.target != "player") { if (calm) ++rep.calmDrifts; else ++rep.drifts; since = 0; }
                else if (!calm) { rep.dwells.push_back(since); dwellEnd = i * kDt; }
                prev = s.target;
            }
            since += kDt;
            if (!calm && dwellEnd >= 0 && s.target == "player" && cocAtPoint(d.geom(), in.player) <= kC0) { rep.returns.push_back(i * kDt - dwellEnd); dwellEnd = -1; }
        }
    }
    // d4: a door slams at 3 s during walk
    {
        focus::Director d = make(focus::Character::Naturalist);
        const D3 door{-7.5, 1.5, -5.0};
        for (int i = 0; i < int(8 / kDt); ++i) {
            const double t = i * kDt;
            focus::Inputs in;
            in.player = at(P[2], 0.9); in.playerMoving = true;
            if (std::abs(t - 3.0) < 0.5 * kDt) in.events.push_back({door, "door", 0.9});
            const focus::State& s = d.step(in, kDt);
            if (rep.eventLatency < 0 && t >= 3.0 - 1e-9 && s.target == "event:door") rep.eventLatency = t - 3.0;
            if (rep.eventLatency >= 0 && rep.eventSettle < 0 && cocAtPoint(d.geom(), door) <= kC0) rep.eventSettle = t - 3.0;
        }
    }
    // d5: the child's jitter over 30 s of walk, and under reduced motion
    for (int calm = 0; calm < 2; ++calm) {
        focus::Director d = make(focus::Character::Child);
        const double K = tiltLook("town_map").K;
        std::vector<double> j;
        double prev = 0;
        for (int i = 0; i < int(30 / kDt); ++i) {
            focus::Inputs in;
            in.player = at(P[2], 0.9); in.playerMoving = true; in.reducedMotion = calm == 1;
            d.step(in, kDt);
            const double jn = K * d.jitterNow;
            if (calm) rep.calmJitter = std::max(rep.calmJitter, std::abs(jn));
            else { if (i > 0) rep.maxJitterStep = std::max(rep.maxJitterStep, std::abs(jn - prev)); j.push_back(jn); }
            prev = jn;
        }
        if (!calm) {
            double best = -1;
            for (int k = 1; k <= 60; ++k) {   // 0.033 Hz steps to 2 Hz
                const double f = k / 30.0;
                double re = 0, im = 0;
                for (std::size_t n = 0; n < j.size(); ++n) { re += j[n] * std::cos(2 * kPi * f * n * kDt); im += j[n] * std::sin(2 * kPi * f * n * kDt); }
                if (re * re + im * im > best) { best = re * re + im * im; rep.jitterHz = f; }
            }
        }
    }
    return rep;
}

}  // namespace raw::art
