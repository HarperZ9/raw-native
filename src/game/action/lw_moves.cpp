// The lw-moves v1 file and the tool strike generator (Low Water combat/data/tools.mjs), ported
// expression for expression so every number comes out with the same bits.
#include "raw/game/action/lw_moves.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace raw::game::action {
namespace {

using raw::story::dump_number;

// JS Math.round: halves round toward +infinity
double js_round(double x) { const double r = std::floor(x); return x - r >= 0.5 ? r + 1 : r; }
double F(double n, double s) { return std::max(1.0, js_round(n * s)); }
double nv(const Json& o, const char* k) { auto* p = o.get(k); return p && p->k == Json::Num ? p->n : 0; }
double at(const Json& o, const char* k, int i) { auto* p = o.get(k); return p && p->k == Json::Arr && (int)p->a.size() > i ? p->a[(size_t)i].n : 0; }
Json str(const std::string& s) { return Json::str(s); }
Json one(Json x) { Json a = Json::arr(); a.add(std::move(x)); return a; }
Json triple(double a, double b, double c) { Json j = Json::arr(); j.add(num(a)); j.add(num(b)); j.add(num(c)); return j; }
Json hit(double a0, double a1, double r1) { Json h = Json::obj(); put(h, "arc", pair(a0, a1)); put(h, "r", pair(0.25, r1)); return h; }
Json dodges(double a, double b) { Json c = Json::obj(); put(c, "step", pair(a, b)); put(c, "roll", pair(a, b)); return c; }
double tell_start(double su, int tell_min) { return std::max(0.0, std::min(3.0, su - tell_min)); }
Json tell(double su, int tell_min) { Json t = Json::obj(); put(t, "at", pair(tell_start(su, tell_min), su - 1)); put(t, "kind", str("swing")); return t; }

Json light_link(const Json& P, int i, int n, double s, const ToolGen& g) {
    const bool last = i == n, w1 = g.windup > 1;
    const double su = std::max(w1 ? (double)g.tell_min : 0.0, F((14 + 2 * (i - 1) + (last && n > 1 ? 3 : 0)) * g.windup, s));
    const double ac = 6, rc = F(22 + (last ? 6 : 0), s), commit = su + ac + 5, end = su + ac + rc - 1;
    const std::string cls = P.str_or("cls");
    Json m = Json::obj();
    put(m, "startup", num(su)); put(m, "active", num(ac)); put(m, "recovery", num(rc)); put(m, "stamina", num(1));
    put(m, "dmg", num(at(P, "light", 0) * (last && n > 1 ? 1.4 : 1))); put(m, "poise", num(at(P, "light", 1) * (last && n > 1 ? 1.5 : 1)));
    put(m, "hits", one(hit(-nv(P, "arc") - (i == 2 ? 15 : 0), nv(P, "arc"), nv(P, "reach") + 0.4)));
    put(m, "root", one(triple(2, su, 1.8 + (last ? 0.8 : 0))));
    put(m, "cancel", dodges(commit, end)); put(m, "track", num(6));
    put(m, "hitstop", num(g.light_hitstop[last ? 1 : 0])); put(m, "knock", num(last ? 2.2 : 1.0)); put(m, "shake", num(g.light_shake[last ? 1 : 0]));
    put(m, "anim", str(cls + ".light_" + std::to_string(i)));
    if (w1) { put(m, "tell", tell(su, g.tell_min)); put(m, "track", num(9)); put(m, "trackUntil", num(su - 3)); }
    put(m, "sweep", pair(su - std::min(su - 1, F(last && n > 1 ? 7 : 4, s)), su));
    if (!last) {
        Json nx = Json::obj(); put(nx, "light", str(g.prefix + "light_" + std::to_string(i + 1))); put(m, "next", nx);
        Json c = *m.get("cancel"); put(c, "light", pair(su + ac + 3, end)); put(m, "cancel", c);
    }
    return m;
}

Json heavy(const Json& P, double s, const ToolGen& g, double& su_out) {
    const std::string sp = P.str_or("special"), cls = P.str_or("cls");
    const double su = F(26 * g.windup, s), rc = F(34, s), wide = sp == "sweep" ? 75 : nv(P, "arc") + 10;
    Json m = Json::obj();
    put(m, "startup", num(su)); put(m, "active", num(6)); put(m, "recovery", num(rc)); put(m, "stamina", num(2));
    put(m, "dmg", num(at(P, "heavy", 0))); put(m, "poise", num(at(P, "heavy", 1)));
    put(m, "hits", one(hit(-wide, wide, nv(P, "reach") + 0.6))); put(m, "root", one(triple(F(16, s), su + 3, 2.0)));
    put(m, "armor", triple(6, su + 4, 1));
    Json c = Json::obj(); put(c, "at", num(F(14, s))); put(c, "max", num(48)); put(c, "min", num(15)); put(c, "into", str(g.prefix + "heavy_charged"));
    put(m, "charge", c); put(m, "cancel", dodges(su + 6 + 6, su + 6 + rc - 1)); put(m, "track", num(4)); put(m, "guardBreak", Json::boolean(true));
    put(m, "hitstop", num(4)); put(m, "knock", num(sp == "pull" ? -3.4 : 3.0)); put(m, "shake", num(0.4));
    put(m, "knockdown", Json::boolean(sp == "pull" || sp == "sweep")); put(m, "anim", str(cls + ".heavy"));
    if (g.windup > 1) { put(m, "tell", tell(su, g.tell_min)); put(m, "track", num(9)); put(m, "trackUntil", num(su - 4)); }
    put(m, "sweep", pair(su - std::min(su - F(14, s) - 1, F(10, s)), su));
    su_out = su;
    return m;
}

Json charged(const Json& P, double s) {
    const std::string sp = P.str_or("special"), cls = P.str_or("cls");
    const double wide = sp == "sweep" ? 75 : nv(P, "arc") + 10;
    Json m = Json::obj();
    put(m, "startup", num(8)); put(m, "active", num(6)); put(m, "recovery", num(F(32, s))); put(m, "stamina", num(1));
    put(m, "dmg", num(at(P, "heavy", 0) * 1.5)); put(m, "poise", num(at(P, "heavy", 1) + 1.5));
    put(m, "hits", one(hit(-wide - 10, wide + 10, nv(P, "reach") + 0.8)));
    put(m, "root", one(triple(0, 10, 3.0))); put(m, "armor", triple(0, 12, 2));
    put(m, "knockdown", Json::boolean(true)); put(m, "guardBreak", Json::boolean(true));
    put(m, "cancel", dodges(24, 8 + 6 + F(32, s) - 1)); put(m, "track", num(2));
    put(m, "hitstop", num(6)); put(m, "knock", num(sp == "pull" ? -4.2 : 4.2)); put(m, "shake", num(0.75));
    put(m, "anim", str(cls + ".heavy_charged")); put(m, "sweep", pair(2, 8));
    return m;
}

}  // namespace

LwMoves load_lw_moves(const std::string& text) {
    Json d = raw::story::parse_json(text);
    if (d.str_or("format") != "lw-moves" || !d.get("version") || d.get("version")->n != 1 || !d.get("fps") || d.get("fps")->n != FPS)
        throw std::runtime_error("lw-moves: expected format lw-moves, version 1, fps 60");
    auto obj = [&](const char* k) { auto* p = d.get(k); if (!p || p->k != Json::Obj) throw std::runtime_error(std::string("lw-moves: missing ") + k); return *p; };
    return {obj("common"), obj("body"), obj("bodyMoveset"), obj("toolClasses"), obj("tools"), obj("foeSpecials"), obj("bossSpecials"), obj("arenaSpecials")};
}

Json tool_profile(const LwMoves& lw, const std::string& tool_id) {
    Json t = lw.tools.get(tool_id) ? *lw.tools.get(tool_id) : Json::obj();
    if (!t.get("cls")) put(t, "cls", str("unarmed"));
    const Json* c = lw.tool_classes.get(t.str_or("cls"));
    if (!c) throw std::runtime_error("lw-moves: no tool class " + t.str_or("cls"));
    Json p = *c;
    for (auto& [k, v] : t.o) put(p, k, v);
    put(p, "id", str(tool_id));
    return p;
}

Json build_tool_moves(const Json& P, const ToolGen& g) {
    const double s = nv(P, "speed") * (g.slow ? 1.3 : 1);
    const int n = (int)nv(P, "chain");
    Json out = Json::obj();
    for (int i = 1; i <= n; i++) put(out, g.prefix + "light_" + std::to_string(i), light_link(P, i, n, s, g));
    double su = 0;
    put(out, g.prefix + "heavy", heavy(P, s, g, su)); put(out, g.prefix + "heavy_charged", charged(P, s));
    Json c = Json::obj(); const double c0 = at(P, "catch", 0), c1 = at(P, "catch", 1);
    put(c, "kind", str("parry")); put(c, "startup", num(c0)); put(c, "active", num(c1 - c0 + 1)); put(c, "recovery", num(20));
    put(c, "stamina", num(1)); put(c, "parry", pair(c0, c1)); put(c, "anim", str(P.str_or("cls") + ".catch"));
    put(out, g.prefix + "catch", c);
    if (g.slow) for (auto& [id, m] : out.o) put(m, "stamina", num(nv(m, "stamina") * 2));
    return out;
}

}  // namespace raw::game::action
