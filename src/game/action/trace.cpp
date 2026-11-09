// Action-timeline trace replay: the inputs of a recorded trace through the C++ Timeline, written
// with the recorder's rules (tools/action/record_timeline.mjs).
#include "raw/game/action/trace.hpp"
#include "trace_detail.hpp"
#include <map>
#include <sstream>
#include <stdexcept>

namespace raw::game::action {
namespace trace_detail {

double n_of(const Json& o, const char* k) {
    auto* p = o.get(k);
    if (!p || p->k != Json::Num) throw std::runtime_error(std::string("trace: missing number ") + k);
    return p->n;
}
bool b_of(const Json& o, const char* k) { auto* p = o.get(k); return p && p->k == Json::Bool && p->b; }

Config config_of(const Json& c) {
    Config g;
    g.buffer = (int)n_of(c, "buffer"); g.catch_lockout = (int)n_of(c, "catchLockout"); g.flinch_guard = (int)n_of(c, "flinchGuard");
    g.poise_delay = (int)n_of(c, "poiseDelay"); g.poise_regen = n_of(c, "poiseRegen"); g.knockdown_poise = n_of(c, "knockdownPoise");
    g.hitstop_light = n_of(c, "hitstopLight"); g.hitstop_heavy = n_of(c, "hitstopHeavy"); g.heavy_from = n_of(c, "heavyFrom");
    g.crowd_at = (int)n_of(c, "crowdAt"); g.crowd_cap = (int)n_of(c, "crowdCap"); g.parried_hold = (int)n_of(c, "parriedHold");
    return g;
}

ActorDef actor_of(const Json& a) {
    ActorDef d;
    d.id = a.str_or("id"); d.team = a.str_or("team");
    d.hp = n_of(a, "hp"); d.stamina = n_of(a, "stamina"); d.poise = n_of(a, "poise");
    d.stamina_regen = n_of(a, "staminaRegen"); d.stamina_delay = (int)n_of(a, "staminaDelay");
    d.stun = n_of(a, "stun"); d.soften = n_of(a, "soften"); d.power = n_of(a, "power");
    d.composure = n_of(a, "composure"); d.composure_max = n_of(a, "composureMax");
    d.steady_light = b_of(a, "steadyLight"); d.keep_feet = b_of(a, "keepFeet");
    d.foot_regen = n_of(a, "footRegen"); d.foot_recovery = (int)n_of(a, "footRecovery");
    if (auto* m = a.get("moveset")) for (auto& [k, v] : m->o) if (v.k == Json::Str) d.moveset.emplace_back(k, v.s);
    if (auto* o = a.get("owns")) for (auto& v : o->a) d.owns.push_back(v.s);
    return d;
}

Json state_of(const Timeline& tl, const Actor& F) {
    Json r = Json::arr();
    r.add(Json::str(F.def.id));
    r.add(F.has_act ? Json::str(tl.table().at(F.act.move).id) : Json());
    for (double v : {F.has_act ? (double)F.act.f : 0.0, F.has_act ? (double)F.act.end : 0.0, F.has_act ? (double)F.act.charge : 0.0,
                     F.has_act && F.act.charging ? 1.0 : 0.0, (double)F.hitstop, (double)F.buf.size(), F.st, F.poise, F.hp, F.composure})
        r.add(num(v));
    return r;
}

}  // namespace trace_detail
namespace {
using namespace trace_detail;
using raw::story::dump_json;
using raw::story::parse_json;

struct Script { Json begin; std::map<int, std::vector<Json>> ins, hits; int end = 0; };

Script read_script(const std::string& text) {
    Script s; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Json r = parse_json(line); const std::string t = r.str_or("t");
        if (t == "abegin") s.begin = r;
        else if (t == "ain") s.ins[(int)n_of(r, "tick")].push_back(r);
        else if (t == "ahit") s.hits[(int)n_of(r, "tick")].push_back(r);
        else if (t == "aend") s.end = (int)n_of(r, "tick");
    }
    if (s.begin.k != Json::Obj) throw std::runtime_error("trace: no abegin record");
    return s;
}

void write_events(std::string& out, const std::vector<Event>& ev, size_t from) {
    for (size_t i = from; i < ev.size(); i++)
        out += dump_json(fields({{"t", Json::str("aev")}, {"tick", num(ev[i].tick)}, {"n", Json::str(ev[i].type)}, {"d", ev[i].d}})) + "\n";
}

void run_tick(Timeline& tl, const Script& s, int k, std::string& out) {
    const size_t mark = out.size();
    if (auto it = s.ins.find(k); it != s.ins.end())
        for (auto& r : it->second) {
            const int a = tl.find_actor(r.str_or("id"));
            if (a < 0) throw std::runtime_error("trace: unknown actor " + r.str_or("id"));
            for (auto& p : r.get("press")->a) tl.press(a, p.s);
            tl.set_hold(a, b_of(r, "hold"));
            out += dump_json(r) + "\n";
        }
    write_events(out, tl.step(), 0);
    if (auto it = s.hits.find(k); it != s.hits.end())
        for (auto& r : it->second) {
            Contact c; c.by = tl.find_actor(r.str_or("by")); c.to = tl.find_actor(r.str_or("to")); c.move = r.str_or("move");
            c.frontal = b_of(r, "frontal"); c.crowd = (int)n_of(r, "crowd"); c.weak = n_of(r, "weak"); c.glance = n_of(r, "glance");
            out += dump_json(r) + "\n";
            const size_t n0 = tl.events().size();
            tl.receive(c);
            write_events(out, tl.events(), n0);
        }
    if (out.size() > mark || k % 4 == 0) {
        Json a = Json::arr(); for (auto& F : tl.actors) a.add(state_of(tl, F));
        out += dump_json(fields({{"t", Json::str("ast")}, {"tick", num(k)}, {"a", a}})) + "\n";
    }
}

}  // namespace

TraceTables load_trace_tables(const std::string& text) {
    Json j = parse_json(text);
    if (!j.get("pool") || !j.get("tables")) throw std::runtime_error("trace tables: expected pool and tables");
    return {*j.get("pool"), *j.get("tables")};
}

MoveTable table_of(const TraceTables& t, const std::string& name) {
    const Json* ids = t.tables.get(name);
    if (!ids) throw std::runtime_error("trace tables: no table " + name);
    Json table = Json::obj();
    for (auto& [id, h] : ids->o) {
        const Json* m = t.pool.get(h.s);
        if (!m) throw std::runtime_error("trace tables: no record " + h.s);
        put(table, id, *m);
    }
    return compile(define_moves(table));
}

TraceResult replay_trace(const std::string& recorded, const TraceTables& tables, const std::function<void(Config&)>& tweak) {
    TraceResult r;
    try {
        Script s = read_script(recorded);
        Config cfg = config_of(*s.begin.get("config"));
        if (tweak) tweak(cfg);
        Timeline tl(table_of(tables, s.begin.str_or("table")), cfg);
        for (auto& a : s.begin.get("actors")->a) tl.add(actor_of(a));
        r.trace = dump_json(s.begin) + "\n";
        for (int k = 1; k <= s.end; k++) run_tick(tl, s, k, r.trace);
        r.trace += dump_json(fields({{"t", Json::str("aend")}, {"tick", num(s.end)}})) + "\n";
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

size_t first_line_difference(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b); std::string l, m; size_t n = 0;
    for (;;) {
        n++;
        const bool ga = (bool)std::getline(x, l), gb = (bool)std::getline(y, m);
        if (!ga && !gb) return 0;
        if (ga != gb || l != m) return n;
    }
}

}  // namespace raw::game::action
