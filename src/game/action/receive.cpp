// What a landed contact does to the timeline and the two receiving meters: Low Water's
// combat/sim/hits.mjs applyHit, parried, takeHit, breakPoise and interruptible, without the parts
// that belong to a game (guards and blocks, seizes, shoves into water, footing, stealth, weak
// points, damage floors, yielding). Same order of operations, so the same bits.
#include "raw/game/action/timeline.hpp"
#include <algorithm>
#include <cmath>

namespace raw::game::action {
namespace {
double js_round(double x) { const double r = std::floor(x); return x - r >= 0.5 ? r + 1 : r; }
bool heavy_class(const Move& m, double heavy_from) {
    return m.id.find("heavy") != std::string::npos || m.id.find("charged") != std::string::npos || m.hitstop >= heavy_from;
}
Json s(const std::string& v) { return Json::str(v); }
}  // namespace

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
    if (tm && tm->in(Sec::Parry, f) && m.parryable && c.frontal) return parried(A, T, m, c.crowd);
    return take_hit(A, T, m, c);
}

std::string Timeline::parried(Actor& A, Actor& T, const Move& m, int crowd) {
    T.has_catch_miss = false;
    T.has_act = false; T.hitstop = stop(true, crowd);
    A.hitstop = cfg_.parried_hold;
    if (int p = table_.find(cfg_.parried); p >= 0) start(A, p);
    A.poise = A.def.poise;
    emit("parry", fields({{"id", s(T.def.id)}, {"by", s(A.def.id)}, {"move", s(m.id)}}));
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
