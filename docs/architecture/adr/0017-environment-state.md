# ADR 0017: one environment state that lighting, effects, materials, particles, audio, animation and gameplay share

**Status:** Proposed (design only; no code beyond the slot-keyed curves in `web/look/keys.mjs`).
**Date:** 2026-10-15. **Builds on:** 0014, 0016.

## Context

The author, 2026-10-15: "you don't have to use the same techniques or anything as ENV. It's
just the approach. You know, the cinematic approach and the ideas and the lighting
composition and the balance between direct and ambient and things like that ... It's about
the granularity that ENV offered. It's not, we don't have to give that to the user, but we
want the engine to have that type of control. And we want to be able to really maximize the
amount of environmental effects and such, and utilize environment in game design as much as
possible, as well as in shaders and ... cross everything over, lighting, effects, environment,
actions, animations, etc." On the same day: "use 8 slots for time of day. Or maybe 6, not 3
though."

Today each pass takes its own parameters. Nothing ties a rain shower to wet surfaces, to wind
in the foliage, to a thunder cue or to a gameplay rule about being seen in the dark.

## Decision

Add one **environment state** as a hub. It is data first: a typed, versioned, serialisable
record that systems read as an immutable per-frame snapshot and change only through logged
commands.

**What it holds** (each term has a unit, a valid range and its own curve):

| Group | Terms |
|---|---|
| Time | hour of day, day of year, time scale; derived sun and moon direction |
| Light | direct colour, intensity and direction; ambient sky and ground colour and intensity; **direct-to-ambient balance** as an explicit term (`direct / (direct + ambient)`); key level; shadow strength; colour temperature |
| Atmosphere | fog density, height falloff and colour; scattering anisotropy; aerial perspective; cloud cover; haze, dust and particulate load |
| Weather | weights over named weather states (clear, overcast, rain, storm, snow, fog, and more as authored), with timed transitions |
| Wind | direction, speed, gustiness, turbulence phase; optional local fields |
| Moisture | rain and snow rate; humidity; surface wetness and snow cover per zone, which accumulate and dry by rule |
| Temperature | air temperature per zone; frost and thaw thresholds |
| Zones | named volumes (an interior, a cave, a doorway) that override terms; an interior is a zone, not a time slot |

**Granularity.** Every term has its own per-slot values and its own range. Slots are 8 by default
and 6 is accepted; fewer than 6 is refused (`web/look/keys.mjs`, periodic monotone cubic, no
overshoot). A term is authored per weather state as well: the value at an hour under a blend of
states is the weighted sum of the states' curves, with weights summing to 1. The composition of
a look is stated, not implied: the author sets key level and the balance (and a colour pair for
key and fill); the engine derives direct and ambient intensity to meet them, or accepts both and
reports the balance. That is the part of the cinematic approach the author named. Control lives
in the engine's API and data; showing any of it to a player is a separate decision.

**Reading.** A consumer calls `snapshot(frame)` and gets a frozen object. It is a pure function
of (seed, inputs applied so far, time). The same inputs give the same snapshot on every
machine, so replays, tests and the CPU reference agree with the GPU.

**Writing.** Gameplay and systems issue typed commands (`setTime`, `setWeatherTarget(state,
seconds)`, `addWind(zone, vector, seconds)`, `wet(zone, amount)`, `heat(zone, degrees, seconds)`).
Commands apply at the frame boundary, in priority order, and each is logged with its source and
reason. A rule-driven update then advances the derived terms: wetness rises with rain and falls
with temperature, wind and low humidity, bounded in [0, 1]; frost forms when temperature is
below the threshold and moisture is present; dust lifts when dry and windy.

**Consumers and their contract.**
- Lighting reads the light group and the balance; shadows read shadow strength.
- Post effects read atmosphere and light: fog, grade and the lens and film stack take their
  intensity from state terms, not from private sliders.
- Materials read wetness, frost, dust and snow cover per surface through an environment layer
  (wet surfaces darken the albedo and lower roughness; frost and dust add layers).
- Particles read wind, weather and moisture for rain, snow, leaves and embers.
- Audio gets an event stream and levels (thunder, wind level, rain on a surface class), with no
  audio engine implied.
- Animation reads wind vectors for cloth and foliage sway, and emits and receives reaction
  events (a shiver below a temperature, a hand to the eyes in glare).
- Gameplay queries `windAt`, `isWet`, `lightAt`, `isDark` for a position, with hysteresis on
  trigger thresholds so a rule does not chatter.

**Process.** The first slice (E0) is time and weather curves, the snapshot, wind, wetness
accumulation and drying, with lighting reading it and one material (wet roughness) as the
consumer, plus a test sandbox. Particles, audio events and animation hooks follow as separate
slices, each with a measurable check.

## Consequences

- The slot-keyed curves already in the repository are the time axis of this state.
- Passes that today carry private intensity parameters get a documented binding to a state
  term; the old parameters stay as overrides for tests.
- Every term needs a unit, a range and a test. A term without one does not enter the state.
- Cost: a CPU update per frame (a few hundred floats) and a uniform buffer of the derived terms.

## Alternatives considered

- Per-system private state: rejected, because rain, wetness and wind would drift apart.
- A fixed stage-and-ini structure like the one ENB uses: not adopted. The author took the
  approach and granularity, not the structure.
- A full fluid or climate simulation: out of scope; the rules above are cheap, bounded and
  authorable.

## What would reverse it

The author's review of the first slice, or a measurement showing the per-frame update or the
uniform traffic costs more than 0.1 ms on the reference scene.
