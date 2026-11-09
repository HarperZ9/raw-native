# One schedule for a blow (`raw/game/action`, `raw/renderer/impact`)

The swing decides the hit, and everything the hit shows or sounds like lands on the tick the hit landed.
Contact comes from a capsule on the animation's swing. The pose, the hit volume, the trail, the smear, the
contact, the hit-stop, the camera shake, the rumble, the sound, the burst and the hitmarker are all cues on
the 60 Hz integer sim tick, built from one event stream. The presentation clock may retime a pose (the
telegraph retiming of [action-hitvol.md](action-hitvol.md)); no cue's tick moves.

```cpp
#include "raw/game/action/schedule.hpp"
using namespace raw::game::action;

Schedule s = schedule_trace(trace, load_trace_tables(tables));   // replay a contact trace, schedule each tick
for (const Cue& c : s.cues) { /* c.tick, c.ch, c.contact: draw or play it */ }
for (const std::string& line : s.check_same_tick()) { /* a cue off its contact's tick */ }
```

```sh
raw_native_cli action-impact contacts/04-w.hammer-zealot.jsonl contacts/tables.json out \
    --from 95 --frames 60 --metres 7 --loop 960      # frame-NNNN.png, contact.wav, loop.gif, loop.webp, run.json
```

## The swing decides the hit

`contact.hpp` is the G3 swing rig's capsule on the shared math: a ring of the move (arc in degrees from
facing, radii r0 to r1, height band, live frames [on, off]) is a blade of radius 0.05 m from r0 + 0.05 to
r1 - 0.05 along a bone that yaws from arc0 on the first live frame to arc1 at the last live frame + 1. Live
frame f tests the sweep over [f, f + 1) at max(8, ceil(yaw a frame / 4 degrees)) samples against the target's
upright cylinder. Every expression runs on `detmath.hpp` in the order of Low Water's `combat/sim/capsule.mjs`,
so the engine and the game find the same contacts on the same tick, bit for bit. `hitvol.hpp` (a posed
skeleton on standard math) stays the reference geometry.

Contact traces are the parity corpus. `tools/action/record_contacts.mjs` runs Low Water's real world with
Lamb and one foe 1.5 m apart, scripted sticks and presses, and lets `W.step` find every contact. The game owns
positions and the damage factors it works out, so the poses contacts were found at (`apos`) and a contact's
weak-point and glance factors (`afac`) are inputs. `replay_contacts` applies the poses, steps the timeline,
finds contacts itself, applies them through `receive`, ports the witness rules that refill Lamb's Composure,
and writes the trace again.

## Channels

| Channel | When | Payload |
|---|---|---|
| Pose | every actor, every tick | move and frame, the retimed clip time, position and facing, the proxy blade's yaw and radii |
| HitVolume | a live ring | the blade segment at the tick's start |
| Trail | an active frame | the blade tip at f and f + 1 |
| Smear | an authored smear frame (the move's `sweep`, in clip time) | the blade's yaw at f and f + 1, its radii |
| Contact | the contact tick | the contact point, heavy or light, the substep that touched |
| Hitstop | the contact tick | the contact hit-stop: 0 light, 2 heavy, capped at 4 in a crowd of three |
| Shake | the contact tick | the blow's `shake` event: amp, frames, direction, spring |
| Rumble | the contact tick | the blow's `rumble` event: low, high, frames |
| Sound | the contact tick | hit.light, hit.heavy, catch or slip, and a gain |
| Effect | the contact tick | a sawtooth burst at the contact point |
| Hitmarker | the contact tick | the contact point and the damage |

A swing that a dodge sees through is found again on later ticks while it overlaps; the sim reports one slip a
swing, and so does the schedule.

## Drawing

`raw/renderer/impact.hpp` is game-agnostic: `ShakeCam` ports the "shake" treatment of Low Water's
`ui/fightcam.mjs` (a keyed shake that peaks on frame 3 plus a spring, one vector limit of 6 px at 1080p and
3 px while a telegraph shows), and a `Canvas` draws the ribbon, the smear wedge, the flat sawtooth burst, the
hitmarker and capsules. `raw/tools/impact_cmd.hpp` turns cues into frames and a 48 kHz sound track whose
voices start on their cue's tick. The bodies are proxy figures until the art's skeletons arrive (G4).

## Verified

| Check | By | Result |
|---|---|---|
| Contact parity | `test_action_contact`, 40 contact traces | 40 of 40 byte-identical, 252 contacts found by capsules; ring contact breaks 39 of 40, a 0.06 m capsule 4 of 40, a flipped byte is caught |
| Kernel against G3 | `test_action_contact` | 1,652,520 targets, 99.998% agree, 32 differ within 1e-9 m of the boundary, 0 otherwise |
| Same tick | `test_action_schedule` | 0 exceptions over 186 contacts; a sound or hitmarker one tick late is caught on all 186 |
| Trail and smear | `test_action_schedule`, `test_action_impact` | 0 trail cues off an active frame, 0 smear cues off an authored frame; in a dump of 24,000 frames the trail covers pixels on 3,269, all active, and on every active frame; kept one frame into recovery it is caught |
| Hit-stop | `test_action_schedule` | 85 light at 0, 77 heavy at 2, none over 4; heavy set to 5 is caught |
| Shake | `test_action_impact` against `tools/action/shake_capture.mjs` | full-weight blow 6.0000 px, 3.0000 under a tell, every frame of 40 traces within 0.000001 px of the JS; a keyed curve stretched 1.5x is 5.3 px off |
| Sound | `test_action_impact` | 186 contact sounds start within 0.05 ms of the frame that shows their burst; two ticks late (33 ms) fails all 186 |
| Rumble | `test_action_schedule` | 162 of 162 JS rumble events equal their cue |
| Flash | Low Water `tools/flashframes.mjs` over rendered frames | a 1080p contact sequence: 0 flashes, 0 red transitions; two full 600-frame runs at 640 x 360: 1 flash a second in one window (the limit is 3), 0 red transitions |

Limits, in plain words: the shake's spring never moves the view by 1 px at 1080p, so the 1 px comparison
checks the keyed curve and the limit, not the spring's size. The flash count of a full run includes the proxy
bodies and the smear crossing a window. No one has watched these sequences for feel; the numbers say when
things land, not whether a blow reads as heavy.
