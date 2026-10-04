# ADR 0003: One frame graph for the GPU renderer and the CPU reference

**Status:** Accepted. **Date:** 2026-10-04.

## Context

A consumer renderer runs dozens of passes per frame. Hand-placed barriers and
hand-managed lifetimes stop scaling long before that; Frostbite's FrameGraph
(GDC 2017), Unreal's RDG, Unity's render graph and Filament's FrameGraph all
answer with the same shape: declare the frame, compile it, run it. raw-native
had six GPU passes with their order and barriers written out in each backend,
and a CPU reference with its own sequence in `render.cpp`.

## Decision

`raw/graph/frame_graph.hpp` is the frame graph, and both renderers run
through it.

- **Setup:** `createBuffer` (owned by the graph), `importBuffer` (owned by the
  caller), `createHost` (data a CPU pass makes), `addPass(name, uses, fn)`,
  `markOutput`.
- **Compile:** validate (no read before a write, no resource twice in a pass,
  no buffer in a graph without a device), cull every pass that writes nothing
  an output or a later kept pass needs, then plan barriers: one per access
  change and one per write, batched per pass.
- **Execute:** create only the buffers kept passes use, record each kept pass
  after its barriers, submit once, wait. A graph with no device runs its passes
  on the CPU.
- **Passes are never reordered.** Declaration order is execution order. This
  keeps the CPU arena's allocation order, and with it the arena certificate,
  unchanged; reordering for overlap is a later, measured change.
- **`describe()`** prints the compiled graph as text, one line per pass, in a
  fixed order. Equal graphs give equal text, which is what a graph certificate
  will hash.
- The GPU renderer derives every pass's accesses from the generated binding
  layout, so a WGSL change that turns a read into a write moves the barriers
  with it.

## Consequences

- Frame-only renders (read back the shaded frame alone) now cull motion and the
  AO estimator the frame is not lit with. Output is unchanged, as the D3D12
  runtime test checks; frame-only timings drop by the culled passes' cost.
- D3D12 now gets one barrier per resource where a hazard exists. Before, it
  got a global UAV barrier after every dispatch. Proven equivalent by the identity
  matrix and by the debug layer reporting no errors across it.
- The CPU reference pays a few `std::function` calls per frame. Not measurable
  against a 64-sample ray-traced pass.

## Not yet here, in the order they are needed

1. Textures and subresources as graph resources.
2. Transient memory aliasing: the compiled plan already records each
   resource's first and last use. Aliasing ends zero-initialized memory, so
   every pass must write its whole output first; the CPU reference catches a
   pass that does not, because the GPU result would drift from it.
3. Cross-frame (history) resources, for TAA and temporal denoisers.
4. Async compute and split barriers, once a backend has a second queue.
5. Parallel command recording.
6. A graph certificate: the hash of `describe()` recorded in `raw-gpu-cert`.

## Alternatives considered

- **Bevy 0.19's approach**, which replaced its render graph with ECS schedules
  per camera. It fits an engine whose everything is ECS; raw-native has no ECS
  and may never (author decision A3).
- **A validating RHI and no graph** (wgpu's per-usage-scope tracking).
  Correct barriers, but no culling, no aliasing and no whole-frame plan to
  certify.

## What would reverse it

If declaring accesses by hand becomes the main source of bugs, the
declarations should come from shader reflection for every pass, as they
already do for compute. If the graph's compile cost shows in profiles at real
pass counts, cache compiled graphs by `describe()` text.
