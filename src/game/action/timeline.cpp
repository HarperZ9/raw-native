// The tick, the buffer and the move step: Low Water's combat/sim/fighter.mjs (consumeBuffer,
// canAct, resolveAction, startMove, stepAction, stepRecovery) and the timeline part of
// sim/world.mjs (gather, heldPress, stepFighter), ported in the same order of operations.
#include "raw/game/action/timeline.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace raw::game::action {

Json fields(std::initializer_list<std::pair<const char*, Json>> kv) {
    Json o = Json::obj();
    for (auto& [k, v] : kv) put(o, k, v);
    return o;
}

namespace {
double js_round(double x) { const double r = std::floor(x); return x - r >= 0.5 ? r + 1 : r; }
}  // namespace

Timeline::Timeline(MoveTable table, Config cfg) : table_(std::move(table)), cfg_(std::move(cfg)) {}

int Timeline::add(const ActorDef& def) {
    Actor F; F.def = def;
    F.hp = def.hp; F.st = def.stamina; F.poise = def.poise;
    F.composure = std::min(def.composure_max, def.composure);
    actors.push_back(std::move(F));
    return (int)actors.size() - 1;
}

int Timeline::find_actor(const std::string& id) const {
    for (size_t i = 0; i < actors.size(); i++) if (actors[i].def.id == id) return (int)i;
    return -1;
}

void Timeline::emit(const std::string& type, Json d) { events_.push_back({tick_, type, sort_keys(d)}); }

const std::vector<Event>& Timeline::step() {
    events_.clear();
    tick_++;
    // gather: a press made once the current move is past its buffer point is held to that move
    for (auto& F : actors) {
        for (auto& a : F.pending) {
            Press p{a, tick_, false, 0};
            if (F.has_act && F.act.f >= table_.at(F.act.move).find(Sec::Buffer)->from) { p.held = true; p.seq = F.act.seq; }
            F.buf.push_back(p);
        }
        F.pending.clear();
    }
    for (auto& F : actors) step_actor(F);
    return events_;
}

void Timeline::step_actor(Actor& F) {
    if (F.hitstop > 0) { F.hitstop--; return; }
    if (F.out || F.fallen) return;
    consume(F);
    if (F.has_act) { step_action(F); if (!F.has_act) consume(F); }
    else F.guard_t = 0;
    if (!F.has_act && F.hp <= 0) { if (F.def.team == "lamb") F.fallen = true; else F.out = true; }
    recover(F);
}

bool Timeline::can_act(const Actor& F, const std::string& a) const {
    if (F.out || F.fallen) return false;
    if (!F.has_act) return true;
    if (F.act.charging) return false;
    return table_.at(F.act.move).cancels(F.act.f, table_.classes.find(a));
}

int Timeline::default_resolve(const Actor& F, const std::string& a) const {
    auto set = [&](const std::string& k) -> const std::string* { for (auto& [x, id] : F.def.moveset) if (x == k) return &id; return nullptr; };
    if (a == "roll") { auto* id = set(a); return id ? table_.find(*id) : -1; }
    if (F.has_act) { int b = table_.classes.find(a); for (auto& [c, to] : table_.at(F.act.move).next) if (c == b) return to; }
    int id = -1;
    if (auto* s = set(a)) id = table_.find(*s);
    else if (std::find(F.def.owns.begin(), F.def.owns.end(), a) != F.def.owns.end()) id = table_.find(a);
    if (id < 0 || table_.at(id).item) return -1;
    return id;
}

int Timeline::consume(Actor& F) {
    std::erase_if(F.buf, [&](const Press& b) { return !((b.held && F.has_act && F.act.seq == b.seq) || tick_ - b.t <= cfg_.buffer); });
    for (int i = (int)F.buf.size() - 1; i >= 0; i--) {
        const std::string a = F.buf[(size_t)i].action;
        if (!can_act(F, a)) continue;
        const int id = resolve ? resolve(*this, F, a) : default_resolve(F, a);
        if (id < 0) continue;
        const Move& m = table_.at(id);
        if (m.find(Sec::Parry) && F.has_catch_miss && tick_ < F.catch_miss) continue;   // a missed parry locks the next one out
        if (m.stamina > 0 && F.st < 1) { emit("breathless", fields({{"id", Json::str(F.def.id)}, {"action", Json::str(a)}})); F.buf.erase(F.buf.begin() + i); continue; }
        F.buf.erase(F.buf.begin() + i);
        start(F, id);
        return id;
    }
    return -1;
}

void Timeline::start(Actor& F, int id) {
    const Move& m = table_.at(id);
    if (m.composure != 0) F.composure = std::max(0.0, F.composure - m.composure);
    if (m.stamina > 0) {
        F.st -= m.stamina; F.winded = F.st <= 0; if (F.st < 0) F.st = 0;
        F.st_wait = F.def.stamina_delay * (F.winded ? 2 : 1);
    }
    const int slow = m.kind == "dodge" ? F.def.foot_recovery : 0;
    const double stun = m.kind == "stagger" || m.kind == "down" ? F.def.stun : 1;
    const int end = (int)js_round(m.total * stun) + slow;
    F.guard_t = 0;
    if (m.find(Sec::Parry)) { F.has_catch_miss = true; F.catch_miss = tick_ + end + cfg_.catch_lockout; }
    F.has_act = true; F.act = Act{}; F.act.move = id; F.act.end = end; F.act.seq = ++F.seq;
    emit("move", fields({{"id", Json::str(F.def.id)}, {"move", Json::str(m.id)}}));
    if (m.find(Sec::Tell))
        emit("tell", fields({{"id", Json::str(F.def.id)}, {"move", Json::str(m.id)}, {"kind", Json::str(m.tell_kind)}, {"frames", num(m.startup)}}));
}

void Timeline::step_action(Actor& F) {
    const Move& m = table_.at(F.act.move);
    Act& A = F.act;
    if (auto* tap = m.find(Sec::TapRelease); tap && A.f >= tap->from && A.f <= tap->to && !F.hold && F.composure >= table_.at(tap->into).composure) {
        start(F, tap->into); return;
    }
    if (auto* c = m.find(Sec::Charge); c && A.f == c->from) {
        if (F.hold && A.charge < c->value) { A.charge++; A.charging = true; return; }
        if (A.charging) { A.charging = false; if (A.charge >= c->min) { start(F, c->into); return; } }
    }
    A.f++;
    if (A.f >= A.end) { F.has_act = false; if (m.then >= 0) start(F, m.then); }
}

void Timeline::recover(Actor& F) {
    const Move* m = F.has_act ? &table_.at(F.act.move) : nullptr;
    if (F.st_wait > 0) F.st_wait--;
    else if (!m || m->kind == "stagger") {
        F.st = std::min(F.def.stamina, F.st + F.def.stamina_regen * F.def.foot_regen * (F.guard_t > 0 ? cfg_.guard_regen : 1) * DT);
        if (F.st > F.def.stamina * 0.3) F.winded = false;
    }
    if (F.poise_wait > 0) F.poise_wait--;
    else F.poise = std::min(F.def.poise, F.poise + cfg_.poise_regen * DT);
}

}  // namespace raw::game::action
