# ADR 0018: renderer first, one C++ core, gameplay in BuildLang, Low Water as the showcase

**Status:** Accepted (D1, D2, D3, D10); D4 to D9 and D11 pending. **Date:** 2026-10-15.
**Builds on:** [0014](0014-game-runtime.md), [0016](0016-style-templates.md),
[0017](0017-environment-state.md). **Amends:** [0007](0007-plugin-model.md) and 0014 on the
scripting language only (see Decision 3).

## Context

On 2026-10-15 the author asked for a plan "to improve it into a full engine and game
release". The first draft of `project-docs/RELEASE-PLAN.md` (PR #147) asked ten decisions,
D1 to D10. It recommended pulling the game-runtime stages ahead of the open renderer
criteria so a playable slice would come early. The author answered four of them on
2026-10-15, relayed by the coordinating session with the answers quoted.

## Decision

The author's answers, quoted, with what each one fixes.

1. **D1, the order of work: "No, renderer first".** The game-runtime stages stay where ROADMAP.md
   and ADR 0014 put them (G1 at M3, G2 at M4, G3 at M5, G4 at M6). Nothing in the game track
   moves ahead of a renderer milestone. The next milestones are renderer milestones on the one
   core: the web-only style passes ported to C++, a native window and swapchain as renderer
   infrastructure, and a Vulkan raster path for Linux and Steam Deck.
2. **D2, the runtime that hosts games: "One C++ core".** One C++ engine, built natively and for
   the browser through WebAssembly. The WGSL passes are shared by both builds. The JavaScript
   modules in `web/shaders/` keep their float64 references as test oracles; the C++ ports must
   match them. A JavaScript game runtime and a desktop build in a system web view are not
   pursued.
3. **D3, how gameplay is authored: "BuildLang from the start".** Gameplay is written in BuildLang
   (HarperZ9/buildlang) from the first game milestone. A data-files-first stage is not used.
   This names BuildLang as the language that ADR 0014's G4 calls "`scripting` through the C
   ABI". The editor stays at M6. What BuildLang needs before it can host gameplay is a design
   study in RELEASE-PLAN.md section 4; nothing is written to the BuildLang repository from
   this one.
4. **D10, the showcase game: "Yes, Low Water".** Low Water (`project-docs/GAME-DESIGN.md`) is the
   showcase game. The title stays a working title.

Pending, with the recommendation from the plan kept for the author:

| ID | Decision | Recommendation in the plan |
|---|---|---|
| D4 | macOS | Defer until after the demo; a Metal backend (ROADMAP M5) or the web build |
| D5 | The game's repository and content licence | A separate private repository until the demo; the licence is the author's |
| D6 | The game's price model | Open; the demo is free either way |
| D7 | A plain statement that games made with raw-native are permitted use under FSL-1.1-MIT | Yes, in the README and the docs |
| D8 | Languages at launch | English, with string tables ready |
| D9 | Spend: the Steam Direct fee, an optional code-signing certificate | The author's call; itch.io needs no spend |
| D11 | A statement that BuildLang's compiled output may ship inside games, commercial ones included (new, from the BuildLang study) | Yes; needed before the public demo |

## Consequences

- RELEASE-PLAN.md is revised: renderer milestones N1 to N3 come next, the game milestones W1 to
  W5 follow the ROADMAP milestones that carry their runtime stages, and a BuildLang integration
  track runs beside them as a design study first.
- The playable vertical slice arrives later than the first draft proposed. It waits for G3
  (save and load) at M5. That is the cost of D1, accepted.
- BuildLang becomes a build-time tool of every game build. It is the author's own project, so
  the dependency policy (ADR 0005) treats it as first party. Its licence for compiled output
  shipped inside a game is not stated in its LICENSE; the plan asks the author for a statement
  (RELEASE-PLAN.md section 4.4).
- The parity matrix keeps its rows; its targets now name the revised milestones.

## Alternatives

- Pull the runtime stages forward (the first draft's D1). Rejected by the author.
- A JavaScript runtime on the web with desktop builds in a web view (the first draft's D2
  alternative). Rejected by the author.
- Data files first and a scripting language later (the first draft's D3 recommendation).
  Rejected by the author.

## What would reverse it

The author's review. For D3, a finding in the BuildLang study that a needed property (a
determinism contract, an embedding path inside the WebAssembly build) cannot be met, reported
to the author with the evidence before any gameplay is written another way.
