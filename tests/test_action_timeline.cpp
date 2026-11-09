// raw/game/action timeline behaviour on a small game-free move table: zero frames from press to
// start when free, the 8-frame buffer (and a planted 7 that loses a press), held presses, cancel
// masks and chains, hit-stop holds, charge, tap-release, and the two receiving meters. Then every
// action Low Water's fighters use, pressed while free, starts on the press tick.
#include "check.hpp"
#include "raw/game/action/trace.hpp"
#include <fstream>
#include <sstream>

using namespace raw::game::action;
using raw::story::parse_json;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

static const char* TABLE = R"({
 "jab": {"startup": 4, "active": 2, "recovery": 6, "stamina": 1, "poise": 1, "hits": [{"arc": [-40, 40], "r": [0, 1]}], "cancel": {"dodge": [8, 11], "light": [6, 11]}, "next": {"light": "jab2"}},
 "jab2": {"startup": 5, "active": 2, "recovery": 8},
 "slow": {"startup": 30, "active": 2, "recovery": 2},
 "big": {"startup": 10, "active": 3, "recovery": 10, "armor": [2, 12, 1], "charge": {"at": 4, "max": 20, "min": 6, "into": "big_charged"}, "tap": {"before": 3, "into": "feint"}},
 "big_charged": {"startup": 3, "active": 3, "recovery": 10},
 "feint": {"startup": 2, "active": 2, "recovery": 4},
 "dodge": {"kind": "dodge", "recovery": 20, "iframes": [2, 10], "cancel": {"light": [14, 19]}},
 "guard": {"kind": "parry", "startup": 2, "active": 6, "recovery": 10, "parry": [2, 7]},
 "flinch": {"kind": "stagger", "recovery": 12},
 "stagger": {"kind": "stagger", "recovery": 40},
 "knockdown": {"kind": "down", "recovery": 60},
 "parried": {"kind": "stagger", "recovery": 30},
 "fall": {"kind": "fall", "recovery": 10}
})";

static Timeline make(Config c = {}) {
    Timeline tl(compile(define_moves(parse_json(TABLE))), c);
    ActorDef d; d.id = "a"; d.team = "lamb"; d.hp = 10; d.stamina = 5; d.poise = 3; d.stamina_regen = 1; d.stamina_delay = 30;
    d.moveset = {{"light", "jab"}, {"heavy", "big"}, {"slow", "slow"}, {"dodge", "dodge"}, {"catch", "guard"}};
    tl.add(d); d.id = "b"; d.team = "foe"; tl.add(d);
    return tl;
}
static std::string act(const Timeline& tl, int i) { auto& F = tl.actors[(size_t)i]; return F.has_act ? tl.table().at(F.act.move).id : "-"; }
static void steps(Timeline& tl, int n) { for (int i = 0; i < n; i++) tl.step(); }
// "slow" runs ticks 1..34 (free on 34); a press made on press_tick in its wind-up: when does "jab" start?
static int started_at(Timeline& tl, int press_tick) {
    tl.press(0, "slow"); tl.step();
    while (tl.tick() + 1 < press_tick) tl.step();
    tl.press(0, "light");
    for (int k = 0; k < 60; k++) { tl.step(); for (auto& e : tl.events()) if (e.type == "move" && e.d.str_or("move") == "jab") return tl.tick(); }
    return -1;
}

static void unit_cases() {
    { Timeline tl = make(); tl.press(0, "light"); tl.step();   // zero frames: the press tick starts the move
      CHECK(tl.tick() == 1 && act(tl, 0) == "jab" && tl.events().size() == 1 && tl.actors[0].act.f == 1); }
    { Timeline a = make(), b = make(), c = make(); Config seven; seven.buffer = 7; Timeline d = make(seven);
      CHECK(started_at(a, 26) == 34);   // 8 frames before free: kept
      CHECK(started_at(b, 25) == -1);   // 9 frames before free, not held: expired
      CHECK(started_at(d, 26) == -1);   // planted buffer 7 loses the 8-frame press
      c.press(0, "slow"); steps(c, 32); c.press(0, "light"); steps(c, 1);   // past the wind-up: held
      CHECK(c.actors[0].buf.size() == 1 && c.actors[0].buf[0].held); steps(c, 1); CHECK(act(c, 0) == "jab"); }
    { Timeline tl = make(); tl.press(0, "light"); steps(tl, 5); tl.press(0, "light"); tl.step();   // f 5: outside light [6, 11]
      CHECK(act(tl, 0) == "jab"); tl.step(); CHECK(act(tl, 0) == "jab2");                         // f 6: the chain
      Timeline u = make(); u.press(0, "light"); steps(u, 8); u.press(0, "dodge"); u.step(); CHECK(act(u, 0) == "dodge"); }
    { Timeline tl = make(); tl.press(0, "light"); tl.step(); tl.actors[0].hitstop = 3;              // hit-stop holds the frame
      steps(tl, 3); CHECK(tl.actors[0].act.f == 1); tl.step(); CHECK(tl.actors[0].act.f == 2); }
    { Timeline tl = make(); tl.press(0, "heavy"); tl.set_hold(0, true); steps(tl, 12);             // charge, then release
      CHECK(tl.actors[0].act.charging && tl.actors[0].act.charge == 8); tl.set_hold(0, false); tl.step(); CHECK(act(tl, 0) == "big_charged");
      Timeline t2 = make(); t2.press(0, "heavy"); t2.set_hold(0, true); steps(t2, 2); t2.set_hold(0, false); t2.step(); CHECK(act(t2, 0) == "feint"); }
    { Timeline tl = make(); int b = 1;                                                                // the two meters
      CHECK(tl.receive({0, b, "jab"}) == "flinch"); steps(tl, 13);
      CHECK(tl.receive({0, b, "jab"}) == "hit");      // inside the flinch guard
      CHECK(tl.actors[1].poise == 1 && tl.actors[1].poise_wait == 120);
      CHECK(tl.receive({0, b, "jab"}) == "stagger" && tl.actors[1].poise == 3 && act(tl, 1) == "stagger"); }
    { Timeline tl = make(); tl.press(1, "heavy"); tl.set_hold(1, true); steps(tl, 5);            // armor tier 1 on frame 4
      CHECK(tl.receive({0, 1, "jab"}) == "armored" && tl.actors[1].poise == 3); }
    { Timeline tl = make(); tl.press(1, "catch"); steps(tl, 3);
      CHECK(tl.receive({0, 1, "jab"}) == "parry" && act(tl, 0) == "parried" && tl.actors[0].hitstop == 12 && act(tl, 1) == "-"); }
    { Timeline tl = make(); tl.press(1, "dodge"); steps(tl, 3); CHECK(tl.receive({0, 1, "jab"}) == "evade"); }
}

static void lw_actions() {
    // every action Low Water's fighters use, pressed while free, starts on the press tick
    const std::string dir = std::string(RAW_SOURCE_DIR) + "/tests/data/action/traces/";
    const TraceTables tables = load_trace_tables(slurp(dir + "tables.json"));
    std::istringstream first(slurp(dir + "01-w.boathook-captain.jsonl")); std::string line; std::getline(first, line);
    const Json begin = parse_json(line);
    int n = 0, ok = 0;
    for (auto& a : begin.get("actors")->a) {
        std::vector<std::string> acts; for (auto& [k, v] : a.get("moveset")->o) acts.push_back(k); for (auto& v : a.get("owns")->a) acts.push_back(v.s);
        for (auto& x : acts) {
            Timeline tl(table_of(tables, begin.str_or("table")));
            ActorDef d; d.id = a.str_or("id"); d.hp = 10; d.stamina = 9; d.poise = 3;
            for (auto& [k, v] : a.get("moveset")->o) d.moveset.emplace_back(k, v.s);
            for (auto& v : a.get("owns")->a) d.owns.push_back(v.s);
            tl.add(d); tl.press(0, x); tl.step(); n++;
            ok += tl.tick() == 1 && tl.actors[0].has_act && !tl.events().empty() && tl.events()[0].type == "move";
        }
    }
    std::printf("actions pressed while free: %d, started on the press tick: %d\n", n, ok);
    CHECK(n > 10 && ok == n);
}

int main() {
    unit_cases();
    lw_actions();
    return raw_test_summary();
}
