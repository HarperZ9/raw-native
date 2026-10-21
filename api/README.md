# API snapshots

raw-native's renderer and scene APIs are published as numbered snapshots
([ADR 0015](../docs/architecture/adr/0015-api-snapshot.md), roadmap M3 criterion 5). A snapshot is
a promise you can check: code written against snapshot N keeps compiling and behaving as
documented until the engine moves to snapshot N + 1, and every such move comes with a migration
note.

The APIs keep maturing between snapshots. Adding a function, a type or a field never breaks a
snapshot. Removing a declaration or changing its text does, and moves the engine to the next
number.

## What a snapshot holds

| File | What it is |
|---|---|
| `snapshot-N/manifest.json` | The headers the snapshot covers, the entry count and the dump's SHA-256 |
| `snapshot-N/dump.txt` | One line per public declaration: `header \| scope \| declaration` |
| `snapshot-N/MIGRATION.md` | From snapshot N - 1 to N: what changed and how to update (from snapshot 2 on) |

The dump is textual. `scripts/api_dump.py` removes comments, whitespace, function bodies and
private members, and splits multi-declarator members, so a line names something a user can call
or read. It is not a compiler's view of the ABI: a change of layout without a change of
declaration is not caught here.

## Snapshot 1

Snapshot 1 (`kApiSnapshot = 1`, 2026-10-11) covers 25 headers (552 declarations): the
scene description (`raw/scene/scene.hpp`) and the renderer layer (materials, lighting, shadows,
GTAO and SSR, TAA, baked lighting, the BVH, rasterization, colour, composite and the GPU
renderer). It leaves out the GPU check fixtures (`*_parity.hpp`, `*_identity.hpp`,
`lighting_scenes.hpp`, `gallery.hpp`), which exist to test the engine, not to build on it.

The plugin C ABI of ADR 0004 and ADR 0007 is not built yet, so neither snapshot has a C ABI part.
Dated note, 2026-10-12: by the coordinator's decision of 2026-10-11, that part of criterion 5 is
deferred until a real plugin consumer exists. ADR 0007 is not accepted just to close the criterion.

## Snapshot 2

`kApiSnapshot = 2` since 2026-10-12. It covers the same 25 headers (562 declarations). Three
grid constants of the anisotropic table changed value when that table became denser and embedded;
[`snapshot-2/MIGRATION.md`](snapshot-2/MIGRATION.md) lists them and the declarations added.

## Snapshot 3

`kApiSnapshot = 3` since 2026-10-14. It covers the same 25 headers (567 declarations). Two defaults
of `raw::post::SsrParams` changed when SSR moved to a per-pixel march with a back layer (method note
14 of `evidence/m3-post-bounds.json`); [`snapshot-3/MIGRATION.md`](snapshot-3/MIGRATION.md) lists
them and the declarations added.

## Checks

- `python scripts/api_dump.py --check` runs in CI. It fails when a declaration of the newest
  snapshot is gone or changed, and names the line.
- `python scripts/api_dump.py --selftest` checks the parser, with controls: a changed parameter
  type and a removed member must register as breaks; an edited function body must not.
- `tests/test_api_snapshot.cpp` is the compatibility suite: code written against the snapshot,
  the way a user writes it, with the contracts the headers document (reciprocity of the material
  model, inverse-square illuminance in lux, cascade ordering, the first TAA frame passing through,
  and so on). It checks behaviour the documentation promises, not the renderer's numbers, so a
  model can improve without breaking the snapshot.

## Moving to the next snapshot

1. Make the breaking change.
2. Run `python scripts/api_dump.py --write N+1` and raise `kApiSnapshot` to N + 1.
3. Write `snapshot-N+1/MIGRATION.md`: each removed or changed declaration and its replacement.
4. Update `tests/test_api_snapshot.cpp` to the new API in the same change.

Dated note (2026-10-18): `RenderOptions` gains `lighting` (2 by default), `ray_ao.hpp` gains
`computeRTVisibility` and `composite.hpp` gains `shadeV2` and `shadeHDRV2`. All three are additions,
so the snapshot number stays. One behaviour changes: with the ray-traced pass on, `renderWithParams`
lights the frame with lighting model 2 (a shadowed sun and the sky's visibility,
`evidence/m3-lighting-v2-bounds.json`). `RenderOptions::lighting = 1` gives the earlier frame, and
`shade` and `shadeHDR` are unchanged.
