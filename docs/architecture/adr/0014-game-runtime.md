# ADR 0014: a game runtime in scope, staged G0 to G4, and an authoring milestone

**Status:** Accepted. **Date:** 2026-10-09. **Amends:** [0009](0009-scope.md) (A3),
[ROADMAP.md](../ROADMAP.md) (adds M6). **Builds on:** 0013.

## Context

ADR 0009 (A3) put the renderer and scene runtime first and kept the `world` and
`scripting` layers empty until a game runtime was put in scope. On 2026-10-09 the
author set the styles-and-genres direction: "Raw Native is going to be an upgraded
modern engine with retro features", meant for people making games like Disco Elysium,
Zero Parades, the Half-Life series, the Kingdom Hearts games and isometric RPGs. The
capability roadmap's revision (section 11) proposed a staged runtime. Asked whether
to adopt it, the author answered: "yes, absolutely, for 100%". Asked about an
authoring milestone: "yes".

## Decision

1. **A game runtime is in scope.** It is staged so that M1 is untouched and each stage
   earns its place with a style template:

   | Stage | Contents | Milestone |
   |---|---|---|
   | G0 | Scene as a transform hierarchy of instances, sprites, tiles and cameras; `superstack.scene/1` import and export | M2 |
   | G1 | Fixed-step main loop, input (keyboard, pointer, gamepad on the web host), camera controllers, picking | M3 |
   | G2 | Audio playback behind an interface, animation state machines, timeline playback of game events | M4 |
   | G3 | The `world` layer: an entity model chosen by an ADR at M4 from the templates' evidence, physics behind an interface, save and load | M5 |
   | G4 | `scripting` through the C ABI, and an editor on the Studio viewer | M6 |

2. **M6, Authoring, joins the milestone plan**: G4, the editor and a template gallery.
3. **The renderer never depends on game code.** The world extracts into the scene each
   frame, the pattern ADR 0009 already names. Gameplay code carries no certificate;
   rendering features that claim `verified` keep their CPU reference.

## Consequences

- `world` and `scripting` gain code from M3 and M5 onwards; `scripts/check_layers.py`
  already has their rules.
- M2 to M5 grow by the stage each one hosts. If a milestone must be trimmed, the
  runtime stage moves before any rendering criterion does.

## Alternatives

- A renderer only, with games left to other engines. Rejected by the author's direction.
- A full runtime at once. Rejected: it would slow M1 to M3, the work the films and the
  Studio depend on.

## What would reverse it

A template that cannot be built without a runtime feature scheduled two milestones
later would pull that feature forward. No user demand for the runtime by the M5 review
would shrink G3 and G4.
