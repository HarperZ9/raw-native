// raw/game/action/schedule: one schedule for everything a blow shows and sounds like (G6; author, 2026-11-09:
// "timing, sound, physics, effects, hitmarkers, etc should all be on the same scheduling").
//
// Every presentation channel is a stream of cues on the 60 Hz integer sim tick, built from the same tick of the
// same event stream the simulation wrote (raw/game/action/contact_trace.hpp TickView). A cue carries the sim
// tick of the event that made it, and a contact id when it belongs to a contact; nothing downstream moves a
// cue's tick. The presentation clock may retime a pose (G5 telegraph retiming: the pose cue's clip time), but
// the pose cue itself stays on its sim tick, as do the hit volume and the contact it decides.
//
// Channels and payloads (v[]):
//   Pose       every actor, every tick: kind move id ("" when free); v0 move frame, v1 clip time (retimed),
//              v2 x, v3 z, v4 facing, v5 the proxy blade's yaw in degrees (attacker space) at the clip time:
//              held back at rest, swung to arc0 over the smear frames, along the ring over its live frames,
//              held at arc1 after; v6, v7 the blade's inner and tip radii (0 when the move has no hit)
//   HitVolume  a live ring: v0..v3 the blade segment (x, z of its two ends) at the tick's start
//   Trail      an active frame (startup <= f < startup + active): v0..v3 the blade tip at f and at f + 1
//   Smear      an authored smear frame (Move::smear_from..smear_to): v0, v1 the blade's yaw (degrees, attacker
//              space) at f and f + 1 on the proxy swing, v2 r0, v3 r1, v4 x, v5 z (actor); kind move id
//   Contact    v0, v1 the contact point; v2 1 for a heavy blow; v3 the substep that touched / substeps
//   Hitstop    v0 the contact hit-stop frames (0 light, 2 heavy, capped in a crowd; 0 for a slipped blow)
//   Shake      from the blow's "shake" event: v0 amp, v1 frames, v2, v3 direction, v4 spring k, v5 damping;
//              kind "catch" for a catch
//   Rumble     from the blow's "rumble" event: v0 low, v1 high, v2 frames
//   Sound      kind: hit.light, hit.heavy, catch or slip; v0 gain
//   Effect     kind burst: v0, v1 the contact point, v2 size, v3 seed
//   Hitmarker  v0, v1 the contact point, v2 damage
// Layer: game.
#pragma once
#include "raw/game/action/contact_trace.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace raw::game::action {

enum class Channel : uint8_t { Pose, HitVolume, Trail, Smear, Contact, Hitstop, Shake, Rumble, Sound, Effect, Hitmarker };
inline constexpr int CHANNELS = 11;
const char* channel_name(Channel c);

struct Cue {
    int tick = 0;
    Channel ch = Channel::Pose;
    int actor = -1, contact = -1;
    bool tell = false;              // Pose: the move's telegraph shows on this tick (the camera holds its shake)
    std::string kind;
    double v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
};

struct ContactRec { int tick = 0, by = 0, to = 0; std::string move, result; double x = 0, z = 0; bool heavy = false; double dmg = 0; int fx = 0; };

// Planted controls: move one channel's cue off its tick, keep the trail into recovery, put a smear on a
// recovery frame. All zero or false in use.
struct ScheduleOpts {
    int sound_late = 0, hitmarker_late = 0, trail_extra = 0;
    bool smear_on_recovery = false;
    bool retime_tells = true;                 // pose clip time through fit_telegraph for moves with a tell
};

struct Schedule {
    std::vector<Cue> cues;
    std::vector<ContactRec> contacts;
    // One line a cue whose tick is not its contact's, and one a contact missing a channel it must have.
    std::vector<std::string> check_same_tick() const;
};

// Appends the cues of one replayed tick.
void schedule_tick(Schedule& s, const TickView& v, const ScheduleOpts& o = {});
// Replays a contact trace and schedules every tick of it.
Schedule schedule_trace(const std::string& recorded, const TraceTables& tables, const ScheduleOpts& o = {}, std::string* error = nullptr);

}  // namespace raw::game::action
