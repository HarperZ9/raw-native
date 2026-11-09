# Action-timeline fixtures: where they come from

Every file here comes from Low Water's JS combat engine (HarperZ9 Low Water repository, branch
`feat/one-schedule-capsules-20261109`, commit `157350027da564c9a8cab518c2b924e39209cf78`, rebased on `dev` ff51f6b; regenerated 2026-11-09, the hooks' 30-frame
lead and capsule contact). The files up to 2026-11-08 came from `dev` 8b49bddde6cf38aaaf1fe4d4f03aae4fb72e5922. They are compared byte for byte, so
`.gitattributes` keeps them as committed (`-text`).

| File | Source | SHA-256 |
|---|---|---|
| `moves.json` | `game/engine/combat/data/moves.json`, the blob | `0ddcb16f38a5fc740e8580f569f89df07faaba8a397ab2a44b11bf401047ba31` |
| `moves-expanded.golden.json` | `game/engine/combat/tests/moves-expanded.golden.json`, the blob | `abf4550640cf16acd9cc39612b2cc04335fc4f04eba3a183f1465f13f717d2a2` |
| `detmath-golden.json` | `game/engine/combat/tests/detmath-golden.json`, the blob | `c7e5240d97bd64b59b8f6ec9996e392ed9ec2da999d1a40119844dcf78d65a34` |
| `frames.txt` | `node tools/action/dump_frames.mjs <combat>` | `9cc70196421ed612e24568ca98569bbb42641e6d1f1e273b5912d7f633d65710` |
| `detmath-sweep.json` | `node tools/action/detmath_sweep.mjs <combat>` | see the file's own hashes |
| `clip-markers.json` | `LW_COMMIT=<commit> node tools/action/export_markers.mjs <combat>` | `bcdc6eb6d448fa2f8a5bd4c57ca406a4cc2606692e76e39ac58e6fffe4a43e0c`; the file also records the SHA-256 of each JS source it read |
| `traces/` | `node tools/action/record_timeline.mjs <combat> traces 40 480` | per trace in `traces/manifest.json` |
| `contacts/` | `node tools/action/record_contacts.mjs <combat> contacts 40 600` (needs `W.contacts` and `W.contactPoses`, Low Water 2026-11-09) | per trace in `contacts/manifest.json` |
| `shake-capture.json` | `node tools/action/shake_capture.mjs <engine> contacts` (`game/engine/ui/fightcam.mjs`) | `0d873c27fdf84ae0f5043f96d7c4feda651aee9d3ee74976511f2b4c95a63f2b` |

`<combat>` is `game/engine/combat` in a checkout of that commit. The recorder ran on Node 25.2.1
and recorded every run twice with identical bytes. Regenerate after a change to the JS engine and
commit the new files with the new source commit in this table.

`clip-warp-vectors.json` comes from this repository, not Low Water: `node tools/action/clip_vectors.mjs`
runs `web/anim/clip.mjs` (SHA-256 `71b4ad1c13e8f0d5e2b720eb81df283f4d8d2552e12d21b8b3c30dc6a3e4d061`)
and writes the vectors `tests/test_clip_phase.cpp` checks the C++ port against.

`clip-markers.json` was exported from the same Low Water commit on Node 25.2.1, twice, with identical
bytes; since 2026-11-09 each clip with a swing travel carries its authored smear frames (`smear`). It needs the whole `game/engine` tree (the combat modules import `game/engine/core`).
