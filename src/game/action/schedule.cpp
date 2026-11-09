// One schedule: see raw/game/action/schedule.hpp.
#include "raw/game/action/schedule.hpp"
#include "raw/game/action/detmath.hpp"
#include "raw/game/action/retime.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>

namespace raw::game::action {
namespace {

constexpr double DEG = 180 / DPI, REST = 100;   // the proxy blade rests 100 degrees behind its arc's start

const char* NAMES[CHANNELS] = {"pose", "hitvolume", "trail", "smear", "contact", "hitstop", "shake", "rumble", "sound", "effect", "hitmarker"};

double js_round(double x) { const double r = std::floor(x); return x - r >= 0.5 ? r + 1 : r; }
bool heavy_class(const Move& m, double heavy_from) {
    return m.id.find("heavy") != std::string::npos || m.id.find("charged") != std::string::npos || m.hitstop >= heavy_from;
}

// The ring's yaw at clip time t, held at its ends outside the live window.
double ring_yaw_at(const Ring& h, double t) {
    const double u = std::clamp((t - h.on) / (double)(h.off + 1 - h.on), 0.0, 1.0);
    return h.arc[0] + (h.arc[1] - h.arc[0]) * u;
}
double rest_yaw(const Ring& h) { return h.arc[0] - (h.arc[1] >= h.arc[0] ? REST : -REST); }

// The proxy swing: rest, then over the smear frames to arc0, then the ring, then held at arc1.
double blade_yaw(const Move& m, double t) {
    if (m.rings.empty()) return -30;
    const Ring& h = m.rings.front();
    if (t >= h.on) return ring_yaw_at(h, t);
    if (m.smear_to >= m.smear_from) {
        const double a = m.smear_from, b = m.smear_to + 1.0;
        if (t >= b) return h.arc[0];
        if (t >= a) return rest_yaw(h) + (h.arc[0] - rest_yaw(h)) * ((t - a) / (b - a));
        return rest_yaw(h);
    }
    return t >= h.on - 1 ? h.arc[0] : rest_yaw(h);
}

void world_at(const Body& A, double r, double yaw_deg, double& x, double& z) {
    const double rad = yaw_deg / DEG, lx = r * std::cos(rad), lz = r * std::sin(rad), c = std::cos(A.face), s = std::sin(A.face);
    x = A.x + lx * c - lz * s; z = A.z + lx * s + lz * c;
}
double tip_r(const Ring& h) { return h.r[0] + CONTACT_RC + std::max(0.0, h.r[1] - h.r[0] - 2 * CONTACT_RC); }

double clip_of(const Move& m, int f, const ScheduleOpts& o) {
    const Section* tell = m.find(Sec::Tell);
    if (!o.retime_tells || !tell || m.startup <= tell->from) return f;
    return clip_time(fit_telegraph(m.startup, tell->from), f);
}

Cue cue(int tick, Channel ch, int actor, int contact, std::string kind, std::initializer_list<double> v) {
    Cue c; c.tick = tick; c.ch = ch; c.actor = actor; c.contact = contact; c.kind = std::move(kind);
    int i = 0; for (double x : v) if (i < 8) c.v[i++] = x;
    return c;
}

void actor_cues(Schedule& s, const TickView& v, size_t i, const ScheduleOpts& o) {
    const Actor& F = v.tl->actors[i];
    const Body& B = (*v.bodies)[i];
    const int k = v.tick, a = (int)i;
    if (!F.has_act) { s.cues.push_back(cue(k, Channel::Pose, a, -1, "", {0, 0, B.x, B.z, B.face, -30})); return; }
    const Move& m = v.tl->table().at(F.act.move);
    const int f = F.act.f;
    const double ct = clip_of(m, f, o);
    const double r_in = m.rings.empty() ? 0 : m.rings.front().r[0] + CONTACT_RC, r_tip = m.rings.empty() ? 0 : tip_r(m.rings.front());
    s.cues.push_back(cue(k, Channel::Pose, a, -1, m.id, {(double)f, ct, B.x, B.z, B.face, blade_yaw(m, ct), r_in, r_tip}));
    s.cues.back().tell = m.in(Sec::Tell, f);
    if (m.rings.empty()) return;
    const Ring& h = m.rings.front();
    for (const Ring& g : m.rings) if (f >= g.on && f <= g.off) {
        const double yaw = ring_yaw(g, f, 0, ring_substeps(g)), r0 = g.r[0] + CONTACT_RC;
        double ax, az, bx, bz; world_at(B, r0, yaw, ax, az); world_at(B, tip_r(g), yaw, bx, bz);
        s.cues.push_back(cue(k, Channel::HitVolume, a, -1, m.id, {ax, az, bx, bz}));
    }
    const int first = m.startup, last = m.startup + m.active - 1 + o.trail_extra;
    if (m.active > 0 && f >= first && f <= last) {
        double x0, z0, x1, z1;
        world_at(B, tip_r(h), ring_yaw_at(h, f), x0, z0); world_at(B, tip_r(h), ring_yaw_at(h, f + 1.0), x1, z1);
        s.cues.push_back(cue(k, Channel::Trail, a, -1, m.id, {x0, z0, x1, z1}));
    }
    const int cf = (int)std::floor(ct);
    const bool authored = m.smear_to >= m.smear_from && cf >= m.smear_from && cf <= m.smear_to;
    const bool planted = o.smear_on_recovery && f == m.startup + m.active;
    if (authored || planted)
        s.cues.push_back(cue(k, Channel::Smear, a, -1, m.id, {blade_yaw(m, ct), blade_yaw(m, ct + 1), h.r[0], tip_r(h), B.x, B.z}));
}

void contact_cues(Schedule& s, const TickView& v, const FoundContact& c, const ScheduleOpts& o) {
    // a swing seen through again on a later tick (receive() reports a slip once a swing) and a blow on a body
    // already down are found but show nothing: one slip a swing, as the sim's events say
    if (c.ev_from == c.ev_to && (c.result == "evade" || c.result == "none")) return;
    const Timeline& tl = *v.tl;
    const Move& m = tl.table().at(c.move);
    const Config& g = tl.config();
    const int k = v.tick, id = (int)s.contacts.size();
    ContactRec rec; rec.tick = k; rec.by = c.by; rec.to = c.to; rec.move = m.id; rec.result = c.result;
    rec.heavy = heavy_class(m, g.heavy_from);
    if (c.at.sample >= 0) { rec.x = c.at.x; rec.z = c.at.z; } else { rec.x = (*v.bodies)[(size_t)c.to].x; rec.z = (*v.bodies)[(size_t)c.to].z; }
    int crowd = 0; for (auto& F : tl.actors) if (F.def.team != "lamb" && !F.out && !F.fallen) crowd++;
    auto stop = [&](bool heavy) { const int f = (int)js_round(heavy ? g.hitstop_heavy : g.hitstop_light); return crowd >= g.crowd_at ? std::min(f, g.crowd_cap) : f; };
    const bool slip = c.result == "evade", caught = c.result == "parry";
    const int hs = slip ? 0 : stop(caught || rec.heavy);
    for (size_t e = c.ev_from; e < c.ev_to; e++) {
        const Event& ev = (*v.events)[e];
        auto n = [&](const char* key) { auto* p = ev.d.get(key); return p && p->k == Json::Num ? p->n : 0.0; };
        if (ev.type == "hit") rec.dmg = n("dmg");
        if (ev.type == "shake") {
            auto* d = ev.d.get("dir"); auto* sp = ev.d.get("spring");
            const double dx = d ? d->a[0].n : 1, dz = d ? d->a[1].n : 0;
            const double kk = sp && sp->get("k") ? sp->get("k")->n : 0, damp = sp && sp->get("damp") ? sp->get("damp")->n : 0;
            s.cues.push_back(cue(ev.tick, Channel::Shake, c.to, id, ev.d.str_or("kind"), {n("amp"), n("frames"), dx, dz, kk, damp}));
            rec.fx++;
        }
        if (ev.type == "rumble") { s.cues.push_back(cue(ev.tick, Channel::Rumble, c.to, id, "", {n("low"), n("high"), n("frames")})); rec.fx++; }
    }
    const double sub = c.at.sample >= 0 ? c.at.sample / (double)ring_substeps(m.rings[(size_t)c.ring]) : 0;
    s.cues.push_back(cue(k, Channel::Contact, c.by, id, c.result, {rec.x, rec.z, rec.heavy ? 1.0 : 0.0, sub}));
    s.cues.push_back(cue(k, Channel::Hitstop, c.to, id, c.result, {(double)hs}));
    const std::string sound = slip ? "slip" : caught ? "catch" : rec.heavy ? "hit.heavy" : "hit.light";
    s.cues.push_back(cue(k + o.sound_late, Channel::Sound, c.to, id, sound, {std::min(1.0, 0.4 + rec.dmg / 3)}));
    const double size = slip ? 0.3 : caught ? 1.2 : rec.heavy ? 1.0 : 0.6;
    s.cues.push_back(cue(k, Channel::Effect, c.to, id, "burst", {rec.x, rec.z, size, (double)(k * 31 + c.by * 7 + c.to)}));
    s.cues.push_back(cue(k + o.hitmarker_late, Channel::Hitmarker, c.to, id, c.result, {rec.x, rec.z, rec.dmg}));
    s.contacts.push_back(rec);
}

}  // namespace

const char* channel_name(Channel c) { return NAMES[(int)c]; }

void schedule_tick(Schedule& s, const TickView& v, const ScheduleOpts& o) {
    for (size_t i = 0; i < v.tl->actors.size(); i++) actor_cues(s, v, i, o);
    for (const FoundContact& c : *v.contacts) contact_cues(s, v, c, o);
}

std::vector<std::string> Schedule::check_same_tick() const {
    std::vector<std::string> out;
    std::vector<int> seen((size_t)contacts.size() * CHANNELS, 0);
    for (const Cue& c : cues) {
        if (c.contact < 0) continue;
        const ContactRec& r = contacts[(size_t)c.contact];
        seen[(size_t)c.contact * CHANNELS + (size_t)c.ch]++;
        if (c.tick != r.tick) {
            std::ostringstream l; l << "contact " << c.contact << " (" << r.move << " at " << r.tick << "): " << channel_name(c.ch) << " cue at " << c.tick;
            out.push_back(l.str());
        }
    }
    const Channel always[] = {Channel::Contact, Channel::Hitstop, Channel::Sound, Channel::Effect, Channel::Hitmarker};
    for (size_t i = 0; i < contacts.size(); i++) {
        for (Channel ch : always) if (seen[i * CHANNELS + (size_t)ch] != 1) out.push_back("contact " + std::to_string(i) + ": " + channel_name(ch) + " cues " + std::to_string(seen[i * CHANNELS + (size_t)ch]));
        const int fx = seen[i * CHANNELS + (size_t)Channel::Shake] + seen[i * CHANNELS + (size_t)Channel::Rumble];
        if (fx != contacts[i].fx) out.push_back("contact " + std::to_string(i) + ": " + std::to_string(fx) + " shake and rumble cues for " + std::to_string(contacts[i].fx) + " events");
    }
    return out;
}

Schedule schedule_trace(const std::string& recorded, const TraceTables& tables, const ScheduleOpts& o, std::string* error) {
    Schedule s;
    ContactOpts co;
    co.on_tick = [&](const TickView& v) { schedule_tick(s, v, o); };
    TraceResult r = replay_contacts(recorded, tables, co);
    if (error) *error = r.error;
    return s;
}

}  // namespace raw::game::action
