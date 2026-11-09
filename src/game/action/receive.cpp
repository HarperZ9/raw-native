// What a landed contact does to the timeline and the two receiving meters: Low Water's
// combat/sim/hits.mjs applyHit, parried, takeHit, breakPoise and interruptible, without the parts
// that belong to a game (guards and blocks, seizes, shoves into water, footing, stealth, weak
// points, damage floors, yielding). Same order of operations, so the same bits.
#include "raw/game/action/timeline.hpp"
#include "raw/game/action/detmath.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace raw::game::action {
namespace {
double js_round(double x) { const double r = std::floor(x); return x - r >= 0.5 ? r + 1 : r; }
bool heavy_class(const Move& m, double heavy_from) {
    return m.id.find("heavy") != std::string::npos || m.id.find("charged") != std::string::npos || m.hitstop >= heavy_from;
}
Json s(const std::string& v) { return Json::str(v); }
}  // namespace

double js_fixed3(double x) {
    if (!(x == x) || x == 0) return 0;
    const bool neg = x < 0;
    char buf[1200];
    auto r = std::to_chars(buf, buf + sizeof buf, neg ? -x : x, std::chars_format::fixed, 1100);   // the exact decimal
    const std::string d(buf, r.ptr);
    const size_t dot = d.find('.');
    long long n = std::atoll(d.substr(0, dot).c_str()) * 1000 + std::atoll(d.substr(dot + 1, 3).c_str());
    if (d[dot + 4] >= '5') n++;     // the nearer n, and on a tie the larger (JS toFixed on |x|)
    const double v = (double)n / 1000;
    return v == 0 ? 0 : neg ? -v : v;
}

Json dir_from(double tx, double tz, double fx, double fz) {
    const double a = datan2(tz - fz, tx - fx);
    Json d = Json::arr(); d.add(num(js_fixed3(dcos(a)))); d.add(num(js_fixed3(dsin(a))));
    return d;
}

void Timeline::blow_fx(const Actor& A, const Actor& T, const Move& m, const Contact& c) {
    if (!cfg_.fx_events) return;
    Json spring = fields({{"k", num(180)}, {"damp", num(14)}});
    if (m.shake > 0)
        emit("shake", fields({{"amp", num(m.shake)}, {"frames", num(6 + js_round(m.hitstop * 1.5))}, {"dir", dir_from(c.tx, c.tz, c.ax, c.az)}, {"spring", spring}}));
    emit("rumble", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}, {"low", num(std::min(1.0, 0.2 + m.shake))},
                           {"high", num(std::min(1.0, 0.1 + m.dmg / 4))}, {"frames", num(4 + m.hitstop * 2)}}));
}

int Timeline::stop(bool heavy, int crowd) const {
    const int f = (int)js_round(heavy ? cfg_.hitstop_heavy : cfg_.hitstop_light);
    return crowd >= cfg_.crowd_at ? std::min(f, cfg_.crowd_cap) : f;
}

std::string Timeline::receive(const Contact& c) {
    Actor& A = actors[(size_t)c.by];
    Actor& T = actors[(size_t)c.to];
    const int to = c.to;
    const int mi = table_.find(c.move);
    if (mi < 0) return "none";
    const Move& m = table_.at(mi);
    if (T.out || T.fallen || T.hp <= 0) return "none";
    const Move* tm = T.has_act ? &table_.at(T.act.move) : nullptr;
    const int f = T.has_act ? T.act.f : -1;
    if (tm && tm->in(Sec::Invulnerable, f)) {
        // each swing is seen through once; a contact with no swing behind it is reported every time
        std::vector<int> fresh; std::vector<int>& seen = A.has_act ? A.act.evaded : fresh;
        if (std::find(seen.begin(), seen.end(), to) == seen.end()) {
            seen.push_back(to);
            emit("evade", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}, {"move", s(m.id)}}));
        }
        return "evade";
    }
    if (A.has_act) A.act.hit.push_back(to);   // each swing hits a body once (hits.mjs findContacts reads it)
    if (tm && tm->in(Sec::Parry, f) && m.parryable && c.frontal) return parried(A, T, m, c);
    return take_hit(A, T, m, c);
}

std::string Timeline::parried(Actor& A, Actor& T, const Move& m, const Contact& c) {
    T.has_catch_miss = false;
    T.has_act = false; T.hitstop = stop(true, c.crowd);
    A.hitstop = cfg_.parried_hold;
    if (int p = table_.find(cfg_.parried); p >= 0) start(A, p);
    A.poise = A.def.poise;
    emit("parry", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}, {"move", s(m.id)}}));
    if (cfg_.fx_events) {   // a catch lands like a door slamming: a hard spring shake, a heavy rumble
        emit("shake", fields({{"kind", s("catch")}, {"amp", num(0.6)}, {"frames", num(40)}, {"dir", dir_from(c.ax, c.az, c.tx, c.tz)},
                              {"spring", fields({{"k", num(260)}, {"damp", num(18)}})}}));
        emit("rumble", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}, {"low", num(1)}, {"high", num(0.8)}, {"frames", num(14)}}));
    }
    return "parry";
}

std::string Timeline::take_hit(Actor& A, Actor& T, const Move& m, const Contact& c) {
    const Move* tm = T.has_act ? &table_.at(T.act.move) : nullptr;
    const double armor = tm && !m.pierce ? tm->armor_at(T.act.f) : 0;
    const double pd = std::max(0.0, m.poise * c.glance - armor);
    const double dmg = m.dmg * c.weak * T.def.soften * c.glance * A.def.power;
    const int crowd = c.crowd;
    T.hp = std::max(0.0, T.hp - dmg); T.poise -= pd;
    if (pd > 0) T.poise_wait = cfg_.poise_delay;
    const int hs = stop(heavy_class(m, cfg_.heavy_from), crowd);
    T.hitstop = std::max(T.hitstop, hs); A.hitstop = std::max(A.hitstop, hs);
    emit("hit", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}, {"move", s(m.id)}, {"dmg", num(dmg)}, {"poise", num(pd)}, {"armor", Json::boolean(armor > 0)}}));
    blow_fx(A, T, m, c);
    if (T.hp <= 0) {
        if (int fm = table_.find(cfg_.fall); fm >= 0) start(T, fm);
        emit("fall", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}}));
        return "fall";
    }
    if ((m.knockdown && armor == 0) || T.poise <= cfg_.knockdown_poise) return break_poise(T, T.def.keep_feet ? cfg_.stagger : cfg_.knockdown);
    if (T.poise <= 0) return break_poise(T, cfg_.stagger);
    if (armor == 0 && pd > 0 && interruptible(T, m)) {
        T.has_flinch = true; T.flinch_at = tick_;
        if (int fl = table_.find(cfg_.flinch); fl >= 0) start(T, fl);
        emit("flinch", fields({{"id", s(T.def.id)}}));
        return "flinch";
    }
    return armor > 0 ? "armored" : "hit";
}

bool Timeline::interruptible(const Actor& T, const Move& blow) const {
    if (T.has_flinch && tick_ - T.flinch_at < cfg_.flinch_guard) return false;
    if (T.def.steady_light && blow.poise < 1.5) return false;
    if (!T.has_act) return true;
    const Move& m = table_.at(T.act.move);
    return (m.kind == "attack" || m.kind == "item") && T.act.f < m.startup;
}

std::string Timeline::break_poise(Actor& T, const std::string& how) {
    T.poise = T.def.poise; T.buf.clear();
    if (int i = table_.find(how); i >= 0) start(T, i);
    emit(how, fields({{"id", s(T.def.id)}}));
    return how;
}

}  // namespace raw::game::action
