# ADR 0013: the media-grade roadmap, and five decisions that go with it

**Status:** Accepted. **Date:** 2026-10-09. **Amends:** [ROADMAP.md](../ROADMAP.md)
(milestone plan replaced). **Builds on:** 0005, 0011, 0012.

## Context

The author's direction, 2026-10-09: "The engine can be deeply improved; we want to
rival and surpass most custom homebrew engines." A capability comparison against
Filament, Bevy, Godot, Wicked Engine and fifteen other renderers, web engines and
media tools ([COMPARISON.md](../COMPARISON.md)) found raw-native ahead on one axis,
verification, and behind on most rendering features. The earlier milestone list put
a native window and a second backend at M3, ahead of what the films, the Studio and
the release media need.

The author accepted the roadmap and its five decisions the same day: "Sounds great,
step all of the rendering up in the engine; we want to compete with all existing
professional grade engines. And ideally, we want to completely mature the engine;
and then showcase the engine on our media and explainers."

## Decision

1. **The milestone plan in [ROADMAP.md](../ROADMAP.md) is replaced** by M1 (a
   media-grade host), M2 (assets and materials), M3 (lit scenes, a frozen API, a
   verified viewer), M4 (scale and reach: Vulkan, native window and HDR swapchain)
   and M5 (light transport and breadth). M0 stays as landed. A1 (D3D12 and web
   first, then Vulkan, then Metal) and A6 (API freeze at M3) still hold.
2. **A5 binds the native translator only.** WGSL that the web host runs as written
   (textures in `web/raw-gpu.mjs`, Motion's shaders) does not go through
   `scripts/wgsl_to_hlsl.py`, so it does not trigger A5. The translator spike,
   growing it or adopting Slang, is an M2 exit criterion.
3. **Muxing stays in ffmpeg for now; an own MP4 muxer comes later.** Mediabunny
   (MPL-2.0) is not vendored: ADR 0005 keeps copyleft out of the engine.
4. **ffmpeg is an external tool, never linked and never shipped** in a release
   archive. The media tools invoke it as a separate process and document it as a
   dependency of the tools, not of the engine.
5. **No binary-only upscalers.** FSR, XeSS and DLSS SDKs are binary-only grants and
   conflict with ADR 0005's reviewable, hash-pinned source rule and with the CPU
   reference. Temporal anti-aliasing is built first (M3), and an own upscaler
   later (M5).

**How progress is shown.** Each milestone's exit criteria are tests or evidence
files. Releases are tagged as milestones land. Every new capability is shown in a
film or explainer scene, and the comparison table is rechecked against each
reference engine's then-current release before any claim of parity or lead.

## Consequences

- M1's work is web first: host textures and compositing, a colour pipeline with a
  C++ reference checked against OpenColorIO offline, vector and text fidelity,
  media output that hashes the same twice, and per-pass budgets.
- The native backends wait for M2 to M4. D3D12 keeps its certificates meanwhile.
- OpenColorIO, HarfBuzz and other tools named in COMPARISON.md enter only as offline
  oracles or build-time scripts, never in the reference path or the default build.

## Alternatives

- **Keep the earlier plan (window and Vulkan at M3):** native reach sooner, but the
  films and the Studio would wait on features they need now.
- **Vendor Mediabunny:** a maintained muxer, at the cost of an MPL dependency.

## Reversal signal

A customer who needs lit 3D models before the Studio is on the shared host moves M2
ahead of the rest of M1. A spike that strongly favours Slang moves the A5 decision
earlier.
