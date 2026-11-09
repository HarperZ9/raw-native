// Contact-trace replay: see raw/game/action/contact_trace.hpp. Line order per tick follows the recorder
// (tools/action/record_contacts.mjs): ain, apos, then each contact found (ahit, its afac), aev, ast.
#include "raw/game/action/contact_trace.hpp"
#include "raw/game/action/detmath.hpp"
#include "raw/game/action/hitvol.hpp"
#include "trace_detail.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace raw::game::action {
using namespace trace_detail;
using raw::story::dump_json;
using raw::story::parse_json;

double wrap_angle(double a) {
    const double TAU = DPI * 2;
    return std::fmod(std::fmod(a + DPI, TAU) + TAU, TAU) - DPI;
}

bool frontal(const Body& T, double from_x, double from_z, double cone_deg) {
    return std::fabs(wrap_angle(datan2(from_z - T.z, from_x - T.x) - T.face)) * (180 / DPI) <= cone_deg;
}

namespace {

struct Side { double radius = 0.35, height = 1.8, regain = 1; bool nose8 = false; };
struct World { double parry_cone = 100, c_catch = 0, c_evade = 0, c_hit = 0, c_riposte = 0; std::vector<Side> side; int lamb = -1, struck_at = -1000000000; };
struct Script { Json begin; std::map<int, std::vector<Json>> ins; std::map<int, Json> pos; std::map<std::tuple<int, std::string, std::string>, Json> fac; int end = 0; };

double n_or(const Json& o, const char* k, double d) { auto* p = o.get(k); return p && p->k == Json::Num ? p->n : d; }

Script read_script(const std::string& text) {
    Script s; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Json r = parse_json(line); const std::string t = r.str_or("t");
        if (t == "abegin") s.begin = r;
        else if (t == "ain") s.ins[(int)n_of(r, "tick")].push_back(r);
        else if (t == "apos") s.pos[(int)n_of(r, "tick")] = r;
        else if (t == "afac") s.fac[{(int)n_of(r, "tick"), r.str_or("by"), r.str_or("to")}] = r;
        else if (t == "aend") s.end = (int)n_of(r, "tick");
    }
    if (s.begin.k != Json::Obj) throw std::runtime_error("contact trace: no abegin record");
    return s;
}

bool gone(const Actor& F) { return F.out || F.fallen || F.hp <= 0; }

// hits.mjs findContacts: every attacker in order with a live hit, every standing opponent its swing has not hit.
std::vector<FoundContact> find_contacts(const Timeline& tl, const std::vector<Body>& bodies, const ContactOpts& o) {
    std::vector<FoundContact> out;
    for (size_t a = 0; a < tl.actors.size(); a++) {
        const Actor& A = tl.actors[a];
        if (!A.has_act || A.hitstop > 0 || gone(A)) continue;
        const Move& m = tl.table().at(A.act.move);
        const int f = A.act.f;
        for (size_t t = 0; t < tl.actors.size(); t++) {
            const Actor& T = tl.actors[t];
            if (T.def.team == A.def.team || gone(T) || std::find(A.act.hit.begin(), A.act.hit.end(), (int)t) != A.act.hit.end()) continue;
            for (size_t i = 0; i < m.rings.size(); i++) {
                const Ring& h = m.rings[i];
                if (f < h.on || f > h.off) continue;
                ContactAt at;
                bool touch;
                if (o.rings) {
                    HitMarker hm; hm.on = h.on; hm.off = h.off;
                    for (int j = 0; j < 2; j++) { hm.arc[j] = h.arc[j]; hm.r[j] = h.r[j]; hm.y[j] = h.y[j]; }
                    const Body& B = bodies[a]; const double dx = bodies[t].x - B.x, dz = bodies[t].z - B.z;
                    const double c = std::cos(B.face), s = std::sin(B.face);
                    touch = ring_hits(hm, dx * c + dz * s, dz * c - dx * s, bodies[t].radius, bodies[t].height);
                } else touch = capsule_contact(bodies[a], h, bodies[t], f, &at, o.rc);
                if (touch) { out.push_back({(int)a, (int)t, A.act.move, (int)i, at, ""}); break; }
            }
        }
    }
    return out;
}

// world.mjs witness, the parts that move numbers the trace records: Lamb's Composure and NOSE 8's Breath.
void witness(Timeline& tl, World& w, const std::vector<Event>& ev, int tick) {
    if (w.lamb < 0) return;
    Actor& L = tl.actors[(size_t)w.lamb];
    const std::string& id = L.def.id;
    auto fill = [&](double n) { L.composure = std::min(L.def.composure_max, L.composure + n * w.side[(size_t)w.lamb].regain); };
    auto str = [](const Event& e, const char* k) { return e.d.str_or(k); };
    for (const Event& e : ev) {
        if (e.type == "evade" && str(e, "id") == id && w.side[(size_t)w.lamb].nose8 && L.has_act && tl.table().at(L.act.move).kind == "dodge")
            L.st = std::min(L.def.stamina, L.st + 1.5);
        if (e.type == "parry" && str(e, "id") == id) fill(w.c_catch);
        const bool held = tick - w.struck_at > 60;
        if (held && e.type == "evade" && str(e, "id") == id) fill(w.c_evade);
        if (e.type == "hit" && str(e, "by") == id) { w.struck_at = tick; fill(str(e, "move") == "riposte" ? w.c_riposte : w.c_hit); }
    }
}

void write_line(std::string& out, const Json& j) { out += dump_json(j) + "\n"; }

}  // namespace

TraceResult replay_contacts(const std::string& recorded, const TraceTables& tables, const ContactOpts& o) {
    TraceResult r;
    try {
        Script s = read_script(recorded);
        const Json& cj = *s.begin.get("config");
        Config cfg = config_of(cj);
        auto* fx = cj.get("fx"); cfg.fx_events = fx && fx->k == Json::Bool && fx->b;
        if (o.tweak) o.tweak(cfg);
        World w; w.parry_cone = n_of(cj, "parryCone");
        w.c_catch = n_of(cj, "composureCatch"); w.c_evade = n_of(cj, "composureEvade"); w.c_hit = n_of(cj, "composureHit"); w.c_riposte = n_of(cj, "composureRiposte");
        Timeline tl(table_of(tables, s.begin.str_or("table")), cfg);
        std::vector<Body> bodies;
        for (auto& a : s.begin.get("actors")->a) {
            const int i = tl.add(actor_of(a));
            if (a.str_or("team") == "lamb") w.lamb = i;
            auto* n8 = a.get("nose8");
            w.side.push_back({n_of(a, "radius"), n_of(a, "height"), n_or(a, "regain", 1), n8 && n8->k == Json::Bool && n8->b});
            bodies.push_back({n_of(a, "x"), n_of(a, "z"), n_of(a, "face"), n_of(a, "radius"), n_of(a, "height")});
        }
        r.trace = dump_json(s.begin) + "\n";
        std::vector<Event> all;
        for (int k = 1; k <= s.end; k++) {
            const size_t mark = r.trace.size();
            if (auto it = s.ins.find(k); it != s.ins.end())
                for (auto& in : it->second) {
                    const int a = tl.find_actor(in.str_or("id"));
                    if (a < 0) throw std::runtime_error("contact trace: unknown actor " + in.str_or("id"));
                    for (auto& p : in.get("press")->a) tl.press(a, p.s);
                    tl.set_hold(a, b_of(in, "hold"));
                    write_line(r.trace, in);
                }
            all = tl.step();
            size_t posed = 0;
            if (auto it = s.pos.find(k); it != s.pos.end()) {
                for (auto& p : it->second.get("p")->a) {
                    const int a = tl.find_actor(p.a[0].s);
                    bodies[(size_t)a].x = p.a[1].n; bodies[(size_t)a].z = p.a[2].n; bodies[(size_t)a].face = p.a[3].n;
                }
                const size_t before = r.trace.size(); write_line(r.trace, it->second); posed = r.trace.size() - before;
            }
            std::vector<FoundContact> found = find_contacts(tl, bodies, o);
            for (FoundContact& c : found) {
                Actor& A = tl.actors[(size_t)c.by];
                Actor& T = tl.actors[(size_t)c.to];
                write_line(r.trace, fields({{"t", Json::str("ahit")}, {"tick", num(k)}, {"by", Json::str(A.def.id)}, {"to", Json::str(T.def.id)}, {"move", Json::str(tl.table().at(c.move).id)}}));
                Contact ct; ct.by = c.by; ct.to = c.to; ct.move = tl.table().at(c.move).id;
                if (auto f = s.fac.find({k, A.def.id, T.def.id}); f != s.fac.end()) { ct.weak = n_of(f->second, "weak"); ct.glance = n_of(f->second, "glance"); write_line(r.trace, f->second); }
                const Body& BA = bodies[(size_t)c.by]; const Body& BT = bodies[(size_t)c.to];
                ct.frontal = frontal(BT, BA.x, BA.z, w.parry_cone);
                ct.crowd = 0; for (auto& F : tl.actors) if (F.def.team != "lamb" && !F.out && !F.fallen) ct.crowd++;
                ct.has_pos = true; ct.ax = BA.x; ct.az = BA.z; ct.tx = BT.x; ct.tz = BT.z;
                const size_t n0 = tl.events().size();
                c.result = tl.receive(ct);
                c.ev_from = all.size();
                all.insert(all.end(), tl.events().begin() + (long)n0, tl.events().end());
                c.ev_to = all.size();
            }
            witness(tl, w, all, k);
            for (const Event& e : all) write_line(r.trace, fields({{"t", Json::str("aev")}, {"tick", num(e.tick)}, {"n", Json::str(e.type)}, {"d", e.d}}));
            if (r.trace.size() - posed > mark || k % 4 == 0) {
                Json a = Json::arr(); for (auto& F : tl.actors) a.add(state_of(tl, F));
                write_line(r.trace, fields({{"t", Json::str("ast")}, {"tick", num(k)}, {"a", a}}));
            }
            if (o.on_tick) o.on_tick({k, &tl, &bodies, &found, &all});
        }
        write_line(r.trace, fields({{"t", Json::str("aend")}, {"tick", num(s.end)}}));
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

}  // namespace raw::game::action
