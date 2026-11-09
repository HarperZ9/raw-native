# Action-timeline fixtures: where they come from

Every file here comes from Low Water's JS combat engine (HarperZ9 Low Water repository, branch
`dev`, commit `8b49bddde6cf38aaaf1fe4d4f03aae4fb72e5922`). They are compared byte for byte, so
`.gitattributes` keeps them as committed (`-text`).

| File | Source | SHA-256 |
|---|---|---|
| `moves.json` | `game/engine/combat/data/moves.json`, the blob | `21948e0e48ccf32ee9f9dfc948b68033a5ec28fec8e0df36bc407efce6571f0c` |
| `moves-expanded.golden.json` | `game/engine/combat/tests/moves-expanded.golden.json`, the blob | `edf2024ac5493c022ae108a7561875b722c297aae41e14d79019f05826a840fb` |
| `detmath-golden.json` | `game/engine/combat/tests/detmath-golden.json`, the blob | `c7e5240d97bd64b59b8f6ec9996e392ed9ec2da999d1a40119844dcf78d65a34` |
| `frames.txt` | `node tools/action/dump_frames.mjs <combat>` | `1695506a8f9f567dfa59471ef7283254cf549957a9ab04a7f8a6c327669ade25` |
| `detmath-sweep.json` | `node tools/action/detmath_sweep.mjs <combat>` | see the file's own hashes |
| `clip-markers.json` | `LW_COMMIT=<commit> node tools/action/export_markers.mjs <combat>` | `602c07f18da2b62f2d089cf20658e8100b1c741828666fe7e51cfad3286df5a8`; the file also records the SHA-256 of each JS source it read |
| `traces/` | `node tools/action/record_timeline.mjs <combat> traces 40 480` | per trace in `traces/manifest.json` |

`<combat>` is `game/engine/combat` in a checkout of that commit. The recorder ran on Node 25.2.1
and recorded every run twice with identical bytes. Regenerate after a change to the JS engine and
commit the new files with the new source commit in this table.

`clip-warp-vectors.json` comes from this repository, not Low Water: `node tools/action/clip_vectors.mjs`
runs `web/anim/clip.mjs` (SHA-256 `71b4ad1c13e8f0d5e2b720eb81df283f4d8d2552e12d21b8b3c30dc6a3e4d061`)
and writes the vectors `tests/test_clip_phase.cpp` checks the C++ port against.

`clip-markers.json` was exported from the same Low Water commit on Node 25.2.1, twice, with identical
bytes. It needs the whole `game/engine` tree (the combat modules import `game/engine/core`).
