# ADR 0001: Layers and enforced include rules

**Status:** Accepted. **Date:** 2026-10-04.

## Context

raw-native 0.5 kept every header in one flat `raw/` directory and every source
file in one `src/`. At 28 headers that worked, but nothing stopped the
certificate code from including the CLI, the renderer from including D3D12, or
a backend from reaching into another. Two such edges already existed: the GPU
adapter type lived in the GPU reconcile header, so the D3D12 device depended on
the renderer, and the receipt writer depended on the CLI's parameters. Mature
engines draw this line explicitly: Godot stacks core, drivers, servers and
scene; Unreal gives every module a list of public and private dependencies;
wgpu splits its API, its validating core and its unsafe backend layer into
separate crates.

## Decision

The source tree is split into layers, one directory each under `raw/` (public
headers) and `src/` (implementation). `app/` belongs to the tools layer. Each
layer may include itself and the layers listed for it in `ARCHITECTURE.md`; the
table is duplicated in `scripts/check_layers.py`, which CI runs on every push
along with a self-test that feeds it one example of every violation class.

Layers with no code yet (platform, assets, audio, world, scripting, editor) are
declared now, so their first file arrives with its rules already set.

`math` is the leaf and `core` sits on it, because the image buffers in core
hold `Vec3` pixels. Either order is defensible; this one needs no code to move.

The checker also enforces:

- graphics and OS API headers only inside their backend directory;
- public headers include public headers only;
- private headers are included only from their own layer, and a backend's
  private headers only from that backend;
- vendored headers only from the directories listed for them;
- no code includes the deprecated 0.5 paths.

Two edges that broke these rules were fixed in the same change. The adapter
type moved to the RHI (`rhi::AdapterInfo`; `GpuAdapterInfo` is now an alias),
and the receipt writer stays in `tools` until it stops taking the CLI's
parameters, at which point it moves to `cert`.

## Consequences

- Every include path changed. The 0.5 paths keep forwarders for one minor
  release ([ADR 0004](0004-api-abi-versioning.md)).
- The checker scans `#include` lines with regular expressions. It cannot see
  an include hidden behind a macro. None exist today; the self-test would
  have to grow if one appears.
- A new layer, or a new edge between layers, now means editing the table in
  two places and writing an ADR. That cost is deliberate.

## Alternatives considered

- **One CMake target per layer.** It would let the linker enforce the rules as
  well. Deferred: it multiplies targets and install exports before there is a
  second consumer of the library, and the include check catches the same edges
  earlier. Revisit when a layer gains its own dependency (for example `assets`
  with a vendored glTF parser).
- **Keep the flat tree and enforce rules by name prefix.** Cheaper, but the
  directory is what a reader sees first, and large engines use
  directories (Godot, Unreal, Filament).

## What would reverse it

A layer that needs to include one above it for a real feature. The answer is
an interface owned by the lower layer, and the upward edge stays forbidden. If
that proves impossible twice, the layering is wrong and a new ADR redraws it.
