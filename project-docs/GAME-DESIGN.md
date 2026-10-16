<!-- writing-profile: research -->
# Showcase game: design document

Dated 2026-10-15. Status: Low Water confirmed by the author on 2026-10-15 ("Yes, Low Water",
ADR 0018); the title stays a working title. Nothing here is built yet
except the engine parts named as built. The release plan that schedules this work is
[RELEASE-PLAN.md](RELEASE-PLAN.md); the engine gaps it depends on are in
[PARITY-MATRIX.md](PARITY-MATRIX.md).

The brief, in the author's words (2026-10-09 and 2026-10-15): a game "more Disco Elysium
and less Rogue Trader", "very, very, very abstract, artistic, grotesque", "very, very
indie", "make it real funky"; a thriller that is scary "but not focused entirely on horror
and on jump scares and fear"; surreal monochrome dithered worlds and painted isometric
places; environment used in game design "as much as possible"; and "something beautiful
and great for people to play and enjoy". Disco Elysium and Zero Parades are favourites.
Those two games are reference for ambition and tone only. Nothing from them is copied:
no names, systems as designed, text, art or audio.

## 1. Three concepts

Each concept was scored against six tests. Fit is the match to the brief above. Showcase
counts how many of the engine's distinctive features carry gameplay.
Reuse is the built work it starts from. Scope is whether one developer with AI agents can
ship a vertical slice inside the release plan. Risk names the largest way it fails.

### A. Low Water (working title): a painted isometric night-shift thriller

A tidal harbour town on the night its great lamp went dark. You are a loss assessor sent by
an insurer that may not exist any more, and your job is to decide whose fault the dark is.
Nobody fired a gun. The tide, the fog, the rain and the lamp itself are the witnesses, and
every one of them changes through the night. You read them with instruments that talk back
to you: a light meter that is too proud, a barometer that predicts gossip, a tide table with
a grudge. The town is grotesque and funny: a fishmongers' union that votes by smell, a
congregation that prays to a broken bulb, a harbourmaster who has been wet for eleven years.

- Look: every place is a painted illustration you walk through (paint-over projection), with
  the film and lens stack for glare off wet stone and for memory scenes. When the power fails,
  a district drops into dither-quantised lighting: two or four tones, lantern pools and a
  sweeping beam, fog lifting to paper white.
- Play: investigation through talk and observation. Checks are not dice. An instrument reads
  the environment state, and what it reads is the evidence. The state changes over the night,
  so the same question has a different answer at dusk and at low tide.

### B. Paper Lantern (working title): a monochrome first-person walk

First person, one bit, a surveyor mapping a town that rearranges itself wherever no light
falls. The lantern's oil is the clock. Something lives in the white of the fog, so safety is
in shadow and danger is in the bright, the reverse of the usual dark-is-danger rule. No
dialogue tree; a few spoken encounters.

- Look: dither-quantised lighting as the whole renderer, fog to paper white, halftone output,
  the floating and inverted architecture of the Salt Road scene.
- Play: route-finding and stealth through tone. The tone level at your position is your
  visibility, read through the same threshold that draws the pattern.

### C. Parish of Small Hours (working title): a 2.5D pixel night parish

A gravedigger keeps the void pools of a hillside parish from rising. Fixed palette, palette
cycling water and fire, long cast shadows on sprites, a lattice spider that weaves light.
Puzzle adventure keyed to moon phase and rain.

- Look: palette-locked lighting, palette-cycle animation, emissive line rendering, pixel
  scaling.
- Play: environmental puzzles over a week of nights.

### Scoring

| Test | A. Low Water | B. Paper Lantern | C. Parish |
|---|---|---|---|
| Fit to the brief | High: Disco Elysium register, thriller without shocks, grotesque and funny, painted isometric | Medium: surreal monochrome and tense; no talk, less "funky" | Medium: indie and strange; weakest thriller and Disco Elysium fit |
| Showcase (features carrying gameplay) | 5 of 5: paint-over, environment state, film and lens, dither lighting (blackouts), RT (wet reflections and light visibility) | 3 of 5: dither lighting, environment (fog, wind), RT visibility | 2 of 5: palette lighting, environment (moon, rain) |
| Reuse of built work | Lamp Quay block-out and paint-over (PR #145), look keys, film and lens passes, Salt Road dither lighting (PR #144, #146), ADR 0017 | Salt Road scene, dither lighting with haze, fog to paper white | Pixel template scaffold, palette dither, hysteresis |
| Scope for one developer plus agents | Medium: art per place is one block-out plus about three paintings; the cost is writing | Low to medium: art is cheap, design of a good chase is hard | Medium: sprite and tile art for every room |
| Largest risk | Writing quality decides the game; AI-drafted prose must reach the author's voice | Thin content reads as a tech demo | Least distinctive in a crowded pixel-art market |

**Pick: A, Low Water**, with B's look kept inside it as the blackout mode. Reasons:

1. It is the author's first-named template (painted isometric, "more Disco Elysium") and
   also satisfies the thriller template's tone. One game serves two of the three ADR 0016
   templates.
2. All five distinctive engine features carry play in it. In B and C some of them would be
   decoration.
3. It starts from the most built work: the paint-over pipeline already round-trips a painted
   harbour at 0 codes error on covered pixels (PR #145, software adapters only), and the
   dither lighting model has GPU parity on SwiftShader (PR #144).
4. A fixed isometric camera bounds the art. A place costs one block-out and a few keyframe
   paintings.
5. Investigation with deterministic checks fits the engine's identity: a world whose state
   can be read, replayed and checked.

The main cost moves from art to writing. Section 8 handles that risk.

## 2. Player fantasy and tone

You are the one person in town whose job is to look carefully. The fantasy is noticing:
reading the wet on a step, the angle of a lamp, who stood where when the fog came in, and
saying it out loud to people who would rather you did not. Tension comes from time, light
and being seen. The game has no combat and no
jump scares. Fear is social and atmospheric: a door that was open at dusk is shut at night,
and someone shut it.

Tone rules:
- Grotesque and warm at once. The town is strange and its people are absurd, and the game
  likes them.
- Funny in the writing. The frame stays serious and painterly.
- No hero arc. The assessor's verdict is a choice with costs.

## 3. Core loop

One loop runs at three scales.

**Moment (seconds).** Move through a painted place. Hover or point the stick at something
and the assessor notices it. Use an instrument on it and get a reading.

**Scene (minutes).** Talk to someone. Lines and options come from what you have noticed and
what the environment says now. A claim you put to someone succeeds or fails on evidence you
hold and the present state. No hidden roll decides it. The game shows why it resolved.

**Night (the session).** Time advances with actions. Each conversation,
search or walk between places costs minutes. The night passes through the time-of-day slots
(8 by default, per the author's 2026-10-15 decision; the slice uses the four from sunset to
dawn). Weather moves toward a scripted target with variation from the seed. At dawn the
assessor files a verdict in the Ledger. The verdict sets the town's state for the next night
in the full game, and ends the vertical slice.

```
notice -> read (instrument x environment state) -> claim (in dialogue or in the Ledger)
   ^                                                        |
   +---- the world changes: time, weather, people react ----+
```

## 4. How the environment drives play

The environment state is ADR 0017 (proposed in PR #145): one typed, versioned record that
lighting, effects, materials, audio, animation and gameplay all read, changed only through
logged commands, with a snapshot that is a pure function of seed, inputs and time. Low Water
is the first consumer that makes every term matter. Each row below is a rule the slice must
implement and test.

| Environment term | What the player sees | What it does to play |
|---|---|---|
| Hour (time-of-day slots) | The grade and light balance shift per slot; lamps come on | People keep routines per slot. Some testimony exists only in one slot. Shops open and close |
| Rain rate and surface wetness | Wet stone darkens and turns glossy; RT or screen-space reflections appear | Rain erases footprints and chalk at a stated rate. Wetness that is still rising tells you a door was opened to the weather recently |
| Fog density and height | Distance dissolves; in blackout, fog lifts to paper white | Fog lets you overhear without being seen. Witnesses cannot have seen what fog hid at that hour, and the instruments can prove it |
| Wind direction and speed | Cloth, smoke, rain angle, sound carried | Speech carries downwind: you hear a conversation from the next quay. Smoke direction dates a fire |
| Tide (a zone term built on the moisture group) | Water level, exposed steps, stranded boats | Low tide opens the quay steps and the drowned lamp's base. The tide table schedules access |
| Light: key level, direct-to-ambient balance | Pools of lamplight, dark gaps | Who can see you. Some people will only speak in the dark; some lie only under the lamp |
| Power (zone light override) | A district drops into dither lighting, two or four tones, beam sweeps | Blackouts make the town honest and dangerous: alibis that relied on light break, and you can move unseen |
| Temperature | Breath, frost on rails at dawn | Frost holds a footprint that rain would have erased. Cold drives people indoors |

Rules for the rules:
- Every gameplay threshold uses hysteresis (ADR 0017), so a rule holds steady at a
  boundary.
- Every rule is testable without rendering: feed a command log, read the snapshot, check the
  rule's output. The CPU and GPU agree on the snapshot because it is data.
- The player can always read the state that drives a rule. The instruments are that
  interface. Nothing in play depends on a value the player cannot inspect.

## 5. Systems

| System | Purpose | Engine part it needs | Built today |
|---|---|---|---|
| Environment state | The hub in section 4 | ADR 0017 slice E0, then wind, tide, power, temperature | Slot-keyed curves only (`web/look/keys.mjs`, PR #145) |
| Painted places | Each place is a block-out with keyframe paintings projected on it | Paint-over projection at runtime, keyframe blend, hole fill | Built as a module with tests and SwiftShader parity (PR #145); not in a runtime |
| Lighting modes | Normal (painted, graded), blackout (dither-quantised) | Look keys, dither lighting pass, a cross-fade between modes | Both passes built separately; no mode switch |
| Movement and camera | Click or stick to move on a navigation mesh; fixed isometric camera per place with small pans | G1 main loop, input, picking (M2 criterion 9 has grid picking) | Picking and occlusion fade built; no loop, no navigation |
| Dialogue | Branching conversation with conditions on facts, environment and instruments | BuildLang dialogue modules, a dialogue UI with readable type at 1280 x 800 | None |
| Instruments | Turn environment state into readings and evidence | Gameplay queries `lightAt`, `isWet`, `windAt` with hysteresis | None |
| Ledger | Evidence, claims and the dawn verdict | Data model, UI | None |
| Schedules | People and their routines by slot and weather | World layer (G3) | None |
| Audio | Ambience by zone and weather, cues on environment events, a score bed | G2 audio playback, mixer; the web mixer exists | Web mixer and worklet built for films; no game playback |
| Save and load | Snapshot of the command log, facts, Ledger and position | G3 save and load | None |
| Settings and accessibility | Section 7 | UI, persistence | None |

Gameplay is BuildLang code (the author's decision D3, "BuildLang from the start", ADR 0018):
movement rules, instruments, dialogue, the Ledger and schedules are BuildLang modules in the
game repository, compiled to C and linked into the engine natively and in WebAssembly. Hot
reload in development builds lets a designer change them without restarting the game. The
integration is the BuildLang track in the release plan, section 4.

## 6. Showcasing the engine, by feature

| Feature | Where it appears in play | Measure that it works |
|---|---|---|
| Paint-over projection | Every place. Camera pans of up to about 30 degrees stay inside the keyframe coverage | Hole fraction under 1% at the pans the game allows, measured as in PR #145 (0.96% with three keyframes at 30 degrees today) |
| Environment state hub | Section 4 | Each rule in section 4 has a test with a command log and an expected outcome |
| Film and lens looks | Glare and halation off wet stone and lamps; memory scenes in a print-film look | The passes' parity bounds hold; a photosensitivity option turns off flicker and weave |
| Dither lighting | Blackouts; the dream of the drowned lamp | GPU levels equal the CPU reference, as now on SwiftShader |
| Ray tracing | Wet reflections and lamp-light visibility on hardware that supports it, with a raster fallback | The fallback is the default; the RT path is an option checked against the CPU path tracer |

RT is optional by design. The game must run on a Steam Deck class device and in a browser,
neither of which can count on hardware RT. RT adds quality where it exists and never gates
play.

## 7. Players, controls and access

- Input: mouse and keyboard, and a gamepad with full parity. Every action reachable on a
  gamepad. Rebinding for both.
- Readability: body text at least 9 px tall at 1280 x 800 (Valve's Steam Deck guidance,
  fetched 2026-10-15; release plan section 4.1), a text size setting, high-contrast
  dialogue panels, subtitles for every voiced line.
- Comfort: a photosensitivity setting that disables flicker, gate weave, film damage and
  strobing lamps; a dither strength setting; no motion blur by default.
- Time: no timed choices. The night advances only with the player's actions.
- Colour: no information carried by colour alone; the instruments state values in words.

## 8. Writing and content

Writing carries this game. Targets, to be revised from measured cost at the vertical slice:

| Content | Vertical slice | Full game |
|---|---|---|
| Districts | 1 (Lamp Quay) | 4 |
| Places (one block-out, about three paintings each) | 4: the quay, the Brine bar, the lamp house, the weigh-house office | about 20 |
| Characters with dialogue | 6 | about 30 |
| Words of dialogue and description | about 12,000 | 70,000 to 100,000 |
| Play time | 45 to 60 minutes | 6 to 10 hours |

The full-game numbers are a target. The slice measures words per day,
paintings per day and test cost per rule, and the full-game scope is set from those numbers
at milestone W3 (release plan).

AI agents may draft, but the author decides the voice, approves every character and owns
the final text. Steam requires a disclosure for AI-generated content; the release plan
carries it. The game's text, art and audio licence is the author's decision (D5 in the
release plan).

## 9. Vertical slice definition

The slice is done when every item passes. It is the playable demo candidate.

1. **Content.** Lamp Quay with four places and six characters, one case (who darkened the
   lamp), three distinct verdicts, about 12,000 words, 45 to 60 minutes for a first play.
2. **Environment.** E0 of ADR 0017 plus tide, power and fog. Every rule in section 4 is in
   play and has a passing test from a command log.
3. **Look.** Every place painted and projected; the four slots from sunset to dawn graded by
   look keys; one blackout in dither lighting; one memory scene in the film look.
4. **Play.** A new player finishes without help in 5 of 8 observed sessions (target, set
   before the sessions run). No soft lock across 1,000 scripted random runs of the command
   log (a fuzz of choices and moves).
5. **Controls and access.** Section 7 complete, including gamepad parity and rebinding.
6. **Save.** Save at any point, quit, load, and the state hash equals the saved one.
7. **Performance.** p95 frame time at most 16.7 ms at 1920 x 1080 on a named mid-tier GPU,
   and at most 33.3 ms at 1280 x 800 on a named low-power device, each over a scripted
   walkthrough.
8. **Builds.** Windows x64, Linux x64 and a web build from a clean checkout in CI
   (Linux follows from milestone N3).
9. **Reliability.** A crash writes a report with the build ID. No crash in the 8 observed
   sessions and the 1,000 fuzz runs.

## 10. What this document does not settle

- The title. "Low Water" is a working title; no trademark search has been run.
- The game's content licence and whether the game repository is public (release plan D5).
- Whether the full game sells or is free (release plan D6).
- The author's reference frames for the painted look (ADR 0016 asks for them).
