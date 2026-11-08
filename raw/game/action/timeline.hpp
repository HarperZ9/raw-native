// raw/game/action/timeline: a fixed 60 Hz integer-tick action timeline for any number of actors.
// Each tick an actor (1) holds still while its hit-stop runs, (2) tries its buffered presses
// newest first against what it may do now (free, or inside the current move's cancel window for
// that action class), (3) steps its current move one frame (charge, tap-release, end and chain),
// and (4) recovers its meters. A press made while the actor is free starts its move on the same
// tick: zero simulation frames from press to move start.
//
// Two receiving meters, as Low Water's combat has them (combat/sim/hits.mjs): a fast interrupt
// (a blow with poise damage interrupts a wind-up or a free stance with a flinch, then the body
// shrugs off flinches for flinch_guard frames) and a slow poise meter (poise damage drains it;
// at 0 the body staggers, at knockdown_poise it goes down; it refills at poise_regen a second
// after poise_delay frames without poise damage). Contact geometry, guards, seizes, footing and
// AI stay with the game: receive() takes a contact the game has already found. Layer: game.
#pragma once
#include "raw/game/action/moves.hpp"
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace raw::game::action {

struct Config {
    int buffer = 8;              // frames a press waits: 133 ms
    int catch_lockout = 24;      // frames after a parry move ends before another may start
    int flinch_guard = 90;
    int poise_delay = 120;
    double poise_regen = 1;      // poise a second
    double knockdown_poise = -2;
    double guard_regen = 0.1;
    double hitstop_light = 0, hitstop_heavy = 2, heavy_from = 5;   // contact hit-stop by class
    int crowd_at = 3, crowd_cap = 4;
    int parried_hold = 12;       // frames a parried attacker is held
    std::string flinch = "flinch", stagger = "stagger", knockdown = "knockdown", parried = "parried", fall = "fall";
};

struct ActorDef {
    std::string id, team;
    double hp = 1, stamina = 1, poise = 1, stamina_regen = 0;
    int stamina_delay = 0;
    double stun = 1;             // scale on stagger and down moves
    double soften = 1, power = 1;   // damage taken scale; damage dealt scale
    double composure = 0, composure_max = 0;
    bool steady_light = false;   // blows under 1.5 poise do not interrupt
    bool keep_feet = false;      // what would knock down only staggers
    double foot_regen = 1; int foot_recovery = 0;
    std::vector<std::pair<std::string, std::string>> moveset;   // action -> move id
    std::vector<std::string> owns;   // move ids that may be pressed by id
};

struct Press { std::string action; int t = 0; bool held = false; int seq = 0; };
struct Act { int move = -1, f = 0, end = 0, charge = 0, seq = 0; bool charging = false; std::vector<int> evaded; };

struct Actor {
    ActorDef def;
    double hp = 0, st = 0, poise = 0, composure = 0;
    int st_wait = 0, poise_wait = 0, hitstop = 0, seq = 0, guard_t = 0;
    bool winded = false, out = false, fallen = false, hold = false;
    bool has_act = false; Act act;
    std::vector<Press> buf;
    bool has_catch_miss = false; int catch_miss = 0;
    bool has_flinch = false; int flinch_at = 0;
    std::vector<std::string> pending;   // presses for the next step
};

struct Event { int tick = 0; std::string type; Json d = Json::obj(); };

// A contact the game found: `by` lands `move` on `to`. frontal: inside the target's front cone.
// crowd: opponents on their feet (caps hit-stop). weak and glance are the game's damage factors
// (a weak point found, a blow glancing off a shield); glance scales poise damage too.
struct Contact { int by = 0, to = 0; std::string move; bool frontal = true; int crowd = 1; double weak = 1, glance = 1; };

class Timeline {
public:
    Timeline(MoveTable table, Config cfg = {});
    int add(const ActorDef& def);
    void press(int actor, const std::string& action) { actors[(size_t)actor].pending.push_back(action); }
    void set_hold(int actor, bool hold) { actors[(size_t)actor].hold = hold; }
    // One tick: presses into buffers, every actor stepped in order. Returns this tick's events.
    const std::vector<Event>& step();
    // Lands a contact. Returns evade, parry, fall, stagger, knockdown, flinch, armored, hit or
    // none; its events are appended to events().
    std::string receive(const Contact& c);
    void start(Actor& F, int move);     // begin a move now (reactions, scripted starts)
    const std::vector<Event>& events() const { return events_; }
    int tick() const { return tick_; }
    const MoveTable& table() const { return table_; }
    const Config& config() const { return cfg_; }
    int find_actor(const std::string& id) const;
    std::vector<Actor> actors;
    // Optional: which move an action means for an actor now (-1: nothing). Default: the current
    // move's chain for the action, then the actor's moveset, then a move it owns by id.
    std::function<int(const Timeline&, const Actor&, const std::string&)> resolve;

private:
    void step_actor(Actor& F);
    int consume(Actor& F);
    bool can_act(const Actor& F, const std::string& a) const;
    int default_resolve(const Actor& F, const std::string& a) const;
    void step_action(Actor& F);
    void recover(Actor& F);
    void emit(const std::string& type, Json d);
    MoveTable table_;
    Config cfg_;
    int tick_ = 0;
    std::vector<Event> events_;
    // receive.cpp
    std::string parried(Actor& A, Actor& T, const Move& m, int crowd);
    std::string take_hit(Actor& A, Actor& T, const Move& m, const Contact& c);
    std::string break_poise(Actor& T, const std::string& how);
    bool interruptible(const Actor& T, const Move& blow) const;
    int stop(bool heavy, int crowd) const;
};

Json fields(std::initializer_list<std::pair<const char*, Json>> kv);

}  // namespace raw::game::action
