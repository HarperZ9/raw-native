# Action timeline (`raw/game/action`)

A fixed 60 Hz action timeline for fighting games, in C++, with no game in it. Moves are counted in
integer simulation frames; each actor holds a buffer of presses, a current move, a hit-stop hold and
two receiving meters. The first game to use it is Low Water, whose JS combat engine is the reference
the C++ is checked against, bit for bit.

```cpp
#include "raw/game/action/lw_moves.hpp"
#include "raw/game/action/timeline.hpp"
using namespace raw::game::action;

LwMoves lw = load_lw_moves(text);                  // an lw-moves v1 file
Json table = build_tool_moves(tool_profile(lw, "w.hammer"), {});
Timeline tl(compile(define_moves(table)));
ActorDef d; d.id = "lamb"; d.stamina = 6; d.poise = 3; d.moveset = {{"light", "light_1"}};
int lamb = tl.add(d);
tl.press(lamb, "light");
for (const Event& e : tl.step()) { /* "move" on this very tick */ }
tl.receive({.by = foe, .to = lamb, .move = "light_1"});   // a contact your game found
```

## The tick

Each `step()` advances the tick by one, puts the presses made since the last step into their
actors' buffers, then steps every actor in order:

1. **Hit-stop.** An actor with a hold left counts it down and does nothing else this tick.
2. **Buffer.** Presses older than `buffer` frames (8, 133 ms) are dropped, unless they were made
   after the current move's buffer point: those are held to that move until it lets them out or ends
   ("buffer early, cancel late"). The rest are tried newest first: an actor that is free, or inside a
   cancel window open to that action's class, starts the move the action resolves to.
3. **Move.** The current move steps one frame: a tap-release window turns it into its `into` when
   the hold button is let go; a charge point holds the frame while the button is down, then becomes
   the charged move once held long enough; on its last frame the move ends and its `then` follows.
   An actor whose move ended this tick tries its buffer again, so a buffered press starts on the
   first free frame.
4. **Meters.** Stamina refills after its delay; poise refills after `poise_delay` frames without
   poise damage.

A press made while the actor is free starts its move on the press tick: zero simulation frames.

## Window sections

| Section | Frames | Meaning |
|---|---|---|
| `Cancel` | from, to, class mask | a buffered action of a class in the mask may cut the move short; `any` sets every bit |
| `Invulnerable` | from, to | contacts pass through (`evade`) |
| `Parry` | from, to | a parryable contact from the front is turned; the attacker is held and starts `parried` |
| `Armor` | from, to, tier | poise damage is reduced by the tier |
| `Charge` | at, max, min, into | hold to charge; release after `min` becomes `into` |
| `TapRelease` | 1 to before, into | let go early and the move becomes `into` |
| `Buffer` | point to end | a press made here is held to the move |
| `Tell` | from, to | the telegraph a renderer shows |
| `Hit` | from, to | a hit volume is live (its geometry is the game's) |

## The two receiving meters

A contact goes through `receive()`. The fast meter is an interrupt: a blow with poise damage that
lands on a free body or a wind-up starts `flinch`, then the body shrugs off flinches for
`flinch_guard` frames (90). The slow meter is poise: damage drains it; at 0 the body staggers, at
`knockdown_poise` (-2) it goes down, and either way it refills to full. Contact hit-stop follows the
blow's class (heavy by name or by an authored hit-stop of 5 frames or more), capped when a crowd is
on its feet. Guards and blocks, seizes, shoves, footing, stealth and weak points stay with the game;
`Contact` carries the factors the game works out (`weak`, `glance`, `crowd`, `frontal`).

## Data

`load_lw_moves` reads the lw-moves v1 file: format, version 1, 60 fps, the fixed tables and the tool
settings. `define_moves` fills every record's defaults exactly as the JS `defineMoves` does, and
`build_tool_moves` ports the JS tool strike generator expression for expression.
`expand_all` produces every move in the shape of Low Water's golden expansion.

## Inspector

```sh
raw_native_cli action-dump table moves.json > frames.txt     # one line a move, sorted
raw_native_cli action-dump strip moves.json base light:heavy  # one move as a frame strip
raw_native_cli action-dump diff a.txt b.txt                   # exit 1 when a move differs
```

```text
base light:heavy startup=21 active=6 recovery=27 free=54 cancel=roll:33-53,step:33-53
frame          0         1         2         3         4         5   |free 54
phase          SSSSSSSSSSSSSSSSSSSSSAAAAAARRRRRRRRRRRRRRRRRRRRRRRRRRR
cancel roll    .................................#####################
armor          ......####################............................
charge         ...........#..........................................
```

## Traces

An action-timeline trace is JSON lines in the form of the story trace (`docs/story-trace.md`).
Inputs: `abegin` (configuration, actors, the move table by name), `ain` (presses and the hold
button, on ticks where either changes), `ahit` (a contact, applied after that tick's step), `aend`.
Outputs: `aev` (timeline events: move, tell, breathless, evade, parry, hit, flinch, stagger,
knockdown, fall) and `ast` (each actor's move, frame, end, charge, hit-stop, buffer length,
stamina, poise, health and composure, on every tick that wrote another line and every fourth tick).
`replay_trace` reads the inputs, runs the timeline and writes the whole trace again.

## Verified

| Check | By | Result |
|---|---|---|
| Frame data of every move: startup, active, recovery, cancel windows, free frame | `test_action_frames` against the JS dump | 230 of 230 equal (104 base, 126 tool strikes); a startup shifted by one frame is reported |
| The whole expansion | `test_action_frames` against the JS golden | byte-identical, 168,234 bytes |
| Trace parity | `test_action_trace`, 40 traces recorded from the JS engine | 40 of 40 byte-identical; a 7-frame buffer breaks 40 of 40; a flipped byte is caught |
| Zero frames when free | `test_action_trace`, `test_action_timeline` | 119 free presses in the traces: 84 started on the press tick, 34 refused for breath, 1 in the parry lockout, 0 other; every action of a real fighter starts on the press tick |
| Buffer of 8 | `test_action_timeline` | a press 8 frames before free starts; 9 before expires; with 7 the 8-frame press is lost |
| Shared math | `test_action_math` | DETMATH's 12 golden bit patterns, and all 8 functions over 1,001 inputs each |

The traces cover the action layer only. Low Water's whole-fight traces (AI, field, hit geometry,
stealth) need those systems ported before they can replay.
