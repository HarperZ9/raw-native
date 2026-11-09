# Hit volumes from clip markers, and telegraph retiming (`raw/game/action`)

Two pieces on top of the [action timeline](action-timeline.md). Clip markers carry a move's timing
on its animation clip, and hit volumes become capsules bound to bones that are live on those
markers. Telegraph retiming lets a move's wind-up play slow and its strike play fast on the
presentation clock, as data, while the simulation keeps its integer frames.

```cpp
#include "raw/game/action/hitvol.hpp"
#include "raw/game/action/retime.hpp"
using namespace raw::game::action;

MarkerSet ms = load_markers(text);                      // raw-clip-markers v1
const MarkerClip& c = *ms.find("base cutter.lash");
for (auto& line : check_markers(c, move)) { /* the clip and the move table disagree */ }

HitVolume v = volume_from_ring(c.hits[0]);              // one capsule on the blade bone
bool hit = capsule_hits(v, tick, x, z, 0.35, 1.8);      // the sweep of this tick against a body

Retime r = fit_telegraph(c.startup(), c.tell_on);       // 0.5x wind-up, then a solved strike
double shown = clip_time(r, tick);                      // the clip frame to draw this tick
RetimeReport rep = lint_retime(c, r);                   // lead, keyed hits, shape
```

## Clip markers

A marker clip names frames on a 60 Hz clip: `active` (the first active frame), `recovery` (the
first recovery frame), `end` (the free frame), the tell window, and each hit window `[on, off]`,
inclusive. `MarkerClip::phases()` reads startup, active and recovery from the markers and lays them
out with `raw::anim::layout`, the C++ port of `web/anim/clip.mjs` in `raw/scene/clip_phase.hpp`.
`raw::anim::warp` maps a frame on one phase layout to the matching frame on another, so an art clip
authored at its own timing plays in step with the markers.

`check_markers` compares a clip with the move table and returns one line a disagreement. The
exporter `tools/action/export_markers.mjs` writes the clips from Low Water's JS engine; the C++ test
reads them against the C++ expansion of the same data, so a clip and a move table that drift apart
fail the test.

## Capsules bound to bones

A ring sector (an arc in degrees from facing, radii r0 to r1, a height band, live as a whole on the
frames of its window) becomes one capsule on a three-joint swing rig: `root`, `swing` at the band's
mid height and `blade` at r0 + 0.05 m along the swing. The capsule runs along the blade to r1 minus
its 0.05 m radius. A raw::anim clip yaws the swing from the arc's start at the on marker to its end
at off + 1, so a one-frame window still sweeps the whole arc. The bones are posed with
`raw::anim::poseSkeleton`.

Live tick f tests the sweep over clip time [f, f + 1): the capsule at max(8, yaw a tick / 4 degrees)
substeps, each against the target's upright cylinder in the ground plane, with the ring's height band
against the cylinder's height. The capsule's own vertical extent is not used. The rig stands in for
the art's bones until a real skeleton carries the blade.

The ring is live on every frame of its window; the capsule reaches a body when the blade passes it.
Over a grid of 316,080 target placements the two agree on whether a body is hit in 96.9% of cases.
Where they differ, the capsule over-reaches at the arc's ends 7,729 times and misses 2,067 times. When
both hit, the capsule lands 1.46 frames later on average, because the ring hits on its first frame.

## Telegraph retiming

A retime is a list of time-scale regions `{value, start, end, ease}` on sim frames `[start, end)`.
Across a region the clip advances `value * (end - start)` frames, shaped by the ease (`linear`, `in`,
`out`, `inout`); outside every region it advances one clip frame a sim frame. `clip_time` is the
presentation clock. The simulation never reads it.

Hit volumes are keyed to the retimed clip: a hit whose markers are `[on, off]` is live on the sim
frames whose clip time reaches `on` and stays before `off + 1` (`sim_frame_at`). A retime that does
not land the clip on the first active frame moves the hit volumes, and the lint reports it.

`fit_telegraph(startup, tell_on)` starts the slow region at the tell onset, so the frames before
it play at 1x and the tell shows on the same frame as before. It splits wind-up and strike on an
integer frame and solves the strike's value so the clip lands on the first active frame. At 0.5x,
the solved strike over Low Water's 211 tell moves runs from 1.5000x to 1.5556x.

`lint_retime` checks one tell move:

| Check | Applies to | Fails when |
|---|---|---|
| `lead` | every tell | retimed first active frame minus retimed tell onset is under the need: 18 frames, 30 when unparryable or unblockable, 36 for a boss signature |
| `lands` | every tell | the clip is not on the first active frame when the sim is |
| `keyed` | every hit | a hit's retimed live frames differ from the move table's |
| `shape` | parryable tells | not a wind-up at 0.25x to 0.75x followed by a strike at 1.2x to 2.0x ending on the first active frame |

```sh
raw_native_cli action-dump tells clip-markers.json [--windup 0.5 --strike 1.5 --ease linear]
```

prints every tell move's need, flat lead, retimed lead and regions, and exits 1 on any issue.

## The readability test

Retiming exists so a player reads the tell. Whether it helps is a question for human eyes, and no
human trial has been run. The test is built and waiting:

- `action-dump trials clip-markers.json trials.json key.json --seed 1 --per-kind 6` picks parryable
  tells with a hit volume (at most six of a kind), and writes each move twice, flat-timed and
  retimed, in a shuffled order. The trial file holds only poses; the key file holds the move, the
  condition and the answer. The committed files in `tools/action/readability/` hold 21 moves, 42
  trials and six answers (swing, pounce, grab, thrust, sweep, throw).
- `tools/action/readability/sheet.html` plays each trial once at 60 fps with a figure 134 px tall,
  stops on the frame the attack would land, and asks the viewer to name the attack. The figure is a
  proxy stick body driven by the clip frame, not the game's art; flat and retimed trials differ in
  timing alone. The flat control is linear timing, not the JS engine's built-in `(f / startup)^1.8`
  wind-up curve.
- `tools/action/readability/analyse.py key.json responses.json ...` counts move-viewer pairs and
  reports each condition's share correct, the paired difference with Newcombe's hybrid score
  interval and the exact McNemar p. It judges only at 20 pairs or more, and calls the retimed tells
  better only when the interval's lower end is above 0. `--selftest` checks it on planted tables with
  known answers.

## Verified

| Check | By | Result |
|---|---|---|
| Marker parity | `test_action_markers`, 230 clips exported from the JS engine against the C++ move table | 0 mismatches over 230 clips, 179 hit windows and 67 tells; a hit marker moved by one frame and an active marker moved by one frame are each reported alone |
| Phases from markers | `test_action_markers` | 230 of 230 equal through `raw::anim::layout` |
| clip.mjs port | `test_clip_phase`, 6 cases and 827 frames from `web/anim/clip.mjs` | bit-identical; one value moved by one ulp is caught |
| Capsule reach and arc | `test_action_hitvol`, 179 rings | all within 5% of the JS rings (0.0000% by construction); bound to the wrong bone, 177 fail; mirrored, the 35 asymmetric arcs fail; a blade 6% longer fails on 44 of 179 only, so the 5% bound is blind to it where r0 is a large share of r1 |
| Retime | `test_action_retime`, 211 tell moves (128 parryable), every ease | no retime shortens a lead; 0 keyed, landing or shape issues; a wind-up started at frame 0 shortens 66 leads; a strike ended a frame early moves 205 hit windows |
| Lead data | the same | no move is short before any retime. Until 2026-11-09 `captain.hook` (24) and `warden.hook` (26) were short of the 30 asked of an unblockable; Low Water moved both to 30. The old startup is planted and caught |

Low Water's sim adopted capsule contact on 2026-11-09, on the shared math: [action-schedule.md](action-schedule.md).

Not run: the blind readability trials. No human has viewed the sheet, and no model judgement stands
in for one.
