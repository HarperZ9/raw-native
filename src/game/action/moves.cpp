// lw-moves v1 records: defaults and checks as the JS defineMoves (combat/sim/moves.mjs), and the
// typed Moves the timeline steps.
#include "raw/game/action/moves.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace raw::game::action {

const char* sec_name(Sec s) {
    static const char* n[] = {"cancel", "invulnerable", "parry", "armor", "charge", "tap-release", "buffer", "tell", "hit"};
    return n[(int)s];
}

int ActionClasses::find(const std::string& name) const {
    for (size_t i = 0; i < names.size(); i++) if (names[i] == name) return (int)i;
    return -1;
}
int ActionClasses::bit(const std::string& name) {
    int i = find(name); if (i >= 0) return i;
    if (names.size() >= 64) throw std::runtime_error("action: more than 64 action classes");
    names.push_back(name); return (int)names.size() - 1;
}

const Section* Move::find(Sec k) const { for (auto& s : sections) if (s.kind == k) return &s; return nullptr; }
bool Move::in(Sec k, int f) const { for (auto& s : sections) if (s.kind == k && f >= s.from && f <= s.to) return true; return false; }
double Move::armor_at(int f) const { for (auto& s : sections) if (s.kind == Sec::Armor && f >= s.from && f <= s.to) return s.value; return 0; }
bool Move::cancels(int f, int b) const {
    // an action with no class of its own (a move pressed by id) is let out only by an "any" window
    for (auto& s : sections)
        if (s.kind == Sec::Cancel && f >= s.from && f <= s.to && (b >= 0 && b < 64 ? (s.mask >> b & 1) != 0 : s.mask == ActionClasses::all())) return true;
    return false;
}

Json num(double v) { return Json::num(v); }
Json pair(double a, double b) { Json j = Json::arr(); j.add(num(a)); j.add(num(b)); return j; }
void put(Json& o, const std::string& key, Json v) {
    for (auto& p : o.o) if (p.first == key) { p.second = std::move(v); return; }
    o.o.emplace_back(key, std::move(v));
}
Json sort_keys(const Json& j) {
    if (j.k == Json::Arr) { Json o = Json::arr(); for (auto& x : j.a) o.add(sort_keys(x)); return o; }
    if (j.k != Json::Obj) return j;
    Json o = Json::obj();
    for (auto& p : j.o) o.o.emplace_back(p.first, sort_keys(p.second));
    std::sort(o.o.begin(), o.o.end(), [](auto& a, auto& b) { return a.first < b.first; });
    return o;
}

namespace {

Json lw_defaults() {
    Json d = Json::obj();
    auto n = [&](const char* k, double v) { put(d, k, num(v)); };
    auto b = [&](const char* k, bool v) { put(d, k, Json::boolean(v)); };
    auto z = [&](const char* k) { put(d, k, Json()); };
    put(d, "kind", Json::str("attack")); n("startup", 0); n("active", 0); n("recovery", 0); n("stamina", 0); n("dmg", 0); n("poise", 0);
    put(d, "hits", Json::arr()); put(d, "root", Json::arr()); z("iframes"); z("armor"); put(d, "cancel", Json::obj()); put(d, "next", Json::obj());
    n("track", 0); z("trackUntil"); n("hitstop", 0); n("knock", 0); n("shake", 0); put(d, "dir", Json::str("facing")); z("parry"); b("parryable", true);
    z("tell"); z("charge"); z("item"); z("then"); b("knockdown", false); z("anim"); b("shove", false); b("grapple", false); b("blockable", true);
    b("guardBreak", false); z("toward"); b("takedown", false); z("tap"); n("composure", 0); b("pierce", false); b("through", false); b("snare", false);
    b("resetRead", false); b("behind", false); n("wide", 0); z("baseTotal"); z("sweep");
    return d;
}

double nv(const Json& o, const char* k) { auto* p = o.get(k); return p && p->k == Json::Num ? p->n : std::nan(""); }
bool truthy(const Json* p) {
    if (!p) return false;
    switch (p->k) { case Json::Null: return false; case Json::Bool: return p->b; case Json::Num: return p->n != 0 && !std::isnan(p->n);
        case Json::Str: return !p->s.empty(); default: return true; }
}
bool is_pair(const Json* p) {
    if (!p || p->k != Json::Arr || p->a.size() < 2) return false;
    for (auto& x : p->a) if (x.k != Json::Num) return false;
    return true;
}

void check(const Json& m, const std::string& id) {
    auto bad = [&](const std::string& why) { throw std::runtime_error("move " + id + ": " + why); };
    for (const char* k : {"startup", "active", "recovery", "stamina", "dmg", "poise", "hitstop"}) if (!(nv(m, k) >= 0)) bad(std::string(k) + " must be 0 or more");
    if (nv(m, "total") < 1) bad("needs at least one frame");
    for (const char* k : {"iframes", "armor", "parry"}) if (truthy(m.get(k)) && !is_pair(m.get(k))) bad(std::string(k) + " must be [from, to]");
    for (auto& [a, w] : m.get("cancel")->o) if (!is_pair(&w)) bad("cancel." + a + " must be [from, to]");
    for (auto& r : m.get("root")->a) if (!(r.k == Json::Arr && r.a.size() == 3)) bad("root entries are [from, to, speed]");
    for (auto& h : m.get("hits")->a) if (!is_pair(h.get("arc")) || !is_pair(h.get("r"))) bad("hits need arc and r");
    const Json* c = m.get("charge");
    if (truthy(c) && !(nv(*c, "at") < nv(m, "startup") && truthy(c->get("into")))) bad("charge.at must fall in startup and name into");
    const Json* s = m.get("sweep");
    if (truthy(s) && !(is_pair(s) && s->a[0].n >= 0 && s->a[1].n == nv(m, "startup") && s->a[0].n < s->a[1].n)) bad("sweep must be [from, startup)");
}

Json define_one(const std::string& id, const Json& raw) {
    Json m = lw_defaults();
    for (auto& [k, v] : raw.o) put(m, k, v);
    put(m, "id", Json::str(id));
    const double su = nv(m, "startup"), ac = nv(m, "active");
    put(m, "total", num(su + ac + nv(m, "recovery")));
    if (!truthy(m.get("anim"))) put(m, "anim", Json::str(id));
    Json hits = Json::arr();
    for (auto& h : m.get("hits")->a) {
        Json o = Json::obj(); Json y = Json::arr(); y.add(num(0)); y.add(num(2.2));
        put(o, "y", y); put(o, "at", pair(su, su + ac - 1));
        for (auto& [k, v] : h.o) put(o, k, v);
        hits.add(o);
    }
    put(m, "hits", hits);
    const Json* tu = m.get("trackUntil");
    if (!tu || tu->k == Json::Null) put(m, "trackUntil", num(su));
    check(m, id);
    return m;
}

}  // namespace

Json define_moves(const Json& table) {
    Json out = Json::obj();
    for (auto& [id, raw] : table.o) put(out, id, define_one(id, raw));
    auto known = [&](const Json* p) { return p && p->k == Json::Str && out.get(p->s); };
    for (auto& [id, m] : out.o) {
        for (auto& [a, to] : m.get("next")->o) if (!known(&to)) throw std::runtime_error("move " + id + ": next names unknown move " + to.s);
        if (truthy(m.get("then")) && !known(m.get("then"))) throw std::runtime_error("move " + id + ": then names unknown move");
        const Json* c = m.get("charge");
        if (truthy(c) && !known(c->get("into"))) throw std::runtime_error("move " + id + ": charge.into names unknown move");
    }
    return out;
}

}  // namespace raw::game::action
