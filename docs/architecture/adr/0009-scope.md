# ADR 0009: Product scope, renderer or engine, and the editor

**Status:** Accepted. These are author decisions A3 and A4. Accepted by the author on 2026-10-04 ("approve all six engine decisions as proposed"). **Date:** 2026-10-04. **Amended 2026-10-09 by [0014](0014-game-runtime.md):** the game runtime is in scope, staged G0 to G4.

## Context

"A mature, potentially production quality consumer renderer, and engine"
covers two products. A **renderer** takes a scene and gives frames: Filament
and bgfx are renderers. An **engine** adds a game runtime on top: an entity
model (ECS or scene tree), physics, animation, audio, scripting, an editor.
Godot, Bevy, Unity and Unreal are engines. Each step from renderer to engine
roughly doubles the surface to build, verify and support.

The layers for both exist in [ARCHITECTURE.md](../ARCHITECTURE.md): `world`,
`scripting` and `editor` are declared, with their include rules, and hold no
code.

## Proposal

**A3, scope:** build the renderer and its scene runtime to consumer quality
first (roadmap M0 to M4): glTF scenes, PBR, shadows, HDR output, a window, two
native backends and the web. Keep `world` and `scripting` empty until the
author decides to build a game runtime, and decide it with evidence from M2:
whether the renderer has users who ask for a runtime.

If the author puts a runtime in scope, the proposal for it:

- an ECS in `world`, extracting into the renderer's scene each frame (the Bevy
  pattern), so the renderer never depends on game code;
- physics through a vetted library behind an interface (Jolt, MIT, which
  supports deterministic simulation **[from research, see GAP-ANALYSIS sources]**),
  with a determinism certificate of its own;
- scripting through the C ABI of [ADR 0004](0004-api-abi-versioning.md)
  before any embedded language.

**A4, editor:** a **verified viewer** first: open a glTF file, render it on
every available backend, show the frame, the CPU reference, the error map and
the certificate side by side. It is the product's demonstration and its first
consumer tool, and it needs the platform layer and swapchain anyway. A scene
editor waits on A3.

## Consequences

- The renderer path to a consumer release is shorter and every step is
  verifiable.
- Competing as an engine (against Godot, Bevy) is deferred, not ceded: the
  layers and rules for it exist, and the roadmap records each engine's lead.

## What would reverse it

The author choosing an engine outright, or M2 users asking for a runtime.
Either reopens this record.
