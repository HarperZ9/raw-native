// raw/game/action/contact_trace: replay of contact traces (tools/action/record_contacts.mjs). The recorder let
// Low Water's JS world find every contact itself with capsules on the swing; the replayer finds them again with
// raw/game/action/contact.hpp and must write the same bytes. The game owns positions and the damage factors it
// works out (a weak point, a glancing shield), so those are inputs:
//   abegin  configuration, actors (with hurt cylinders and start poses), the move table by name
//   ain     presses and the hold button            apos  the poses contacts are found at, on change
//   afac    a contact's weak and glance factors    aend  the last tick
// Outputs: ahit (each contact found, in the order found), aev (timeline events, with each blow's shake and
// rumble), ast (each actor's timeline state). The witness rules of combat/sim/world.mjs that refill Lamb's
// Composure (a catch, a slipped blow, a landed blow) and Breath (NOSE 8 on an evade) are ported here, beside
// the replay, because they are the game's and not the timeline's. Layer: game.
#pragma once
#include "raw/game/action/contact.hpp"
#include "raw/game/action/trace.hpp"
#include <functional>
#include <string>
#include <vector>

namespace raw::game::action {

// One contact found on a tick: who, by which move and ring, where, what receive() made of it, and the span of
// the tick's events [ev_from, ev_to) that it emitted.
struct FoundContact { int by = 0, to = 0, move = -1, ring = 0; ContactAt at; std::string result; size_t ev_from = 0, ev_to = 0; };

// What one tick looked like, for a presentation layer that schedules from the same stream.
struct TickView {
    int tick = 0;
    const Timeline* tl = nullptr;
    const std::vector<Body>* bodies = nullptr;       // the poses this tick's contacts were found at
    const std::vector<FoundContact>* contacts = nullptr;
    const std::vector<Event>* events = nullptr;      // every timeline event of the tick, in order
};

struct ContactOpts {
    double rc = CONTACT_RC;                 // planted checks widen the capsule
    bool rings = false;                     // planted check: the old whole-window ring decides instead
    std::function<void(Config&)> tweak;     // planted checks on the configuration
    std::function<void(const TickView&)> on_tick;
};

TraceResult replay_contacts(const std::string& recorded, const TraceTables& tables, const ContactOpts& o = {});

// combat/sim/fighter.mjs wrapAngle, onto [-pi, pi), and the frontal cone test of hits.mjs.
double wrap_angle(double a);
bool frontal(const Body& T, double from_x, double from_z, double cone_deg);

}  // namespace raw::game::action
