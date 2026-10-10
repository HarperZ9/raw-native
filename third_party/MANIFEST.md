# Third-party manifest

This file lists every piece of third-party code or data in raw-native: what it is, where it comes from, its licence, the files of ours that use it, and the SHA-256 of every file. `scripts/check_third_party.py` runs in CI. It fails when a listed file changes or goes missing, when a file under `third_party/` is not listed, or when superstack's own `SUPERSTACK.sha256` disagrees with this list. To update a dependency, replace its files, run `python scripts/check_third_party.py --print`, and paste the new block below in the same commit.

The dependency policy is ADR 0005. In brief:

- No MPL, GPL or binary-only libraries.
- No code copied from GPL references.
- ffmpeg is never linked or shipped.

## Vendored, byte for byte

| Library | Upstream and version | SPDX licence | Files | Adapter files (ours) |
|---|---|---|---|---|
| superstack | github.com/HarperZ9/superstack, tag `v0.2.0` | FSL-1.1-MIT | `third_party/superstack/` (see `NOTICE.md` there) | `raw/tools/receipt.hpp`, `app/main.cpp` (receipts); `web/sound/contract.mjs`, `fixtures.mjs`, `meter.mjs` (seed rule, BS.1770 weighting) |

superstack is by the same author as raw-native and ships under the same licence. It is listed here because it is versioned and released on its own.

## Ported, not vendored

| Source | Upstream and version | SPDX licence | Our port | Adapter files (ours) |
|---|---|---|---|---|
| OpenColorIO, the ACES 2.0 output transform | github.com/AcademySoftwareFoundation/OpenColorIO, `v2.6.0` (commit 10a43ec1df03d072ca9a9e2fb3f26e454b0900d8), `src/OpenColorIO/ops/fixedfunction/ACES2/` | BSD-3-Clause | `src/renderer/aces2.hpp`, `aces2_cam.cpp`, `aces2_gamut.cpp`, `aces2_tables.cpp`; `web/colour/colour.wgsl` (its ACES 2.0 part) | `raw/renderer/colour.hpp`, `web/colour/` |

No OpenColorIO code is linked. The notice is in `third_party/NOTICE-OpenColorIO.md`. OpenColorIO itself runs only offline, in `tools/colour/ocio_compare.py`, to measure the port.

## Data derived from a font

| Font | Upstream | SPDX licence | Our file | Made by |
|---|---|---|---|---|
| Hanken Grotesk, weight 500 | github.com/marcologous/hanken-grotesk | OFL-1.1 | `docs/motion/hanken-grotesk-500.atlas.json` (glyph outlines and kerning; the copyright line and licence URL are inside) | `scripts/glyph_atlas.py` |

## Open items

These were found while writing this manifest and are not resolved here:

- **The Hanken Grotesk font version is not recorded.** The atlas does not record which release of the font file it was cut from. The next atlas build should write the font's version string and the SHA-256 of its file into the atlas.
- **The full OFL-1.1 text is not in the repository.** The atlas carries the copyright line and the licence URL. The OFL asks that the licence travel with the font software, and whether a derived glyph atlas needs the full text beside it is not settled here. Adding the text from the upstream repository needs a download, which waits for the author's approval.
- **The Threads shader names no source.** `src/renderer/gpu/shaders/threads.wgsl` says it was "ported from a GLSL thread renderer" but does not name which. If that renderer is the author's own, it is first party and belongs nowhere in this file. If it is not, it needs a row here.

## Hashes

```sha256
1b9e0dfd2b54dfe3210cb2d985db85b559c3f91233ccdb4750369dbfca7eac0b  third_party/NOTICE-OpenColorIO.md
1d1cc85b096776d3996d62b06a6eed2304fe756601d9796974818c2fd9b6b9bc  third_party/superstack/LICENSE.txt
fa72735ac03a0f6a6dbe08c7097530570b7a1be10fedb195daef13ebfbcf6c4b  third_party/superstack/NOTICE.md
30ddc178fe40be3f9490312fa140fed70cfd5576f7a9dd8c8bd74881ff248096  third_party/superstack/SUPERSTACK.sha256
ff9c7b11c0cc1e6a324fc58e0e91a0a627533538ccfa97e9341a439919261ec9  third_party/superstack/examples/sound/sound.json
8c553e7db8f89fae128b6d9dac2012a17191c74d14022332a3c395b3a961a3b2  third_party/superstack/examples/sound/sound_exact.mjs
a0d223c0567c883b73ed74b19d93cb087e213dc14b950ab0f6f04206cb72c590  third_party/superstack/superstack.hpp
b972095e4eadb007fb892e48027fe6d970328eb2256749e046b296f500208878  third_party/superstack/superstack.mjs
693400f92d1396a036535bb1bcf057b691a973c3a5f3b1fa365d54fa9c58d8d2  third_party/superstack/tests/run_vectors.cpp
a4672aad9c14e0de9733bd6af8462504e91783acf1caaf43640e4fd3abaae4ed  third_party/superstack/vectors/MANIFEST.json
395112b48140f455f80793e289491dc76333607734099c3995f095312320a8f1  third_party/superstack/vectors/canonical.json
96fc2e066e9e32af4a76dbfa8c6af7d4fbd73e3378d7ca58433e78f9cc9e3bba  third_party/superstack/vectors/clock.json
5a956f0dda3cc21cce6e9d9308a6470f2a4a72ed7822d999f0dad10595db76da  third_party/superstack/vectors/colour.json
b3607b0a84dd91cffb870497d4199363991f4ed12e7257776ec9019b2ce3d003  third_party/superstack/vectors/export.json
7bc56833582b968ca826ee4c1e5cba4f9841c702f2441053679c2d45be777dcf  third_party/superstack/vectors/hash.json
bbdb40c25e46afad5e651278d6065d834fd4bad3e8a3d38bc7e7052e46687a95  third_party/superstack/vectors/receipt.json
883971c3f3533f578a99fbe883457b16293150262d1420fb4ab8673af689d71a  third_party/superstack/vectors/reconcile.json
d34238647d294c609b1b94eaed9e55f444801367681be825b533efd616c97ac7  third_party/superstack/vectors/scene.json
e87d3f28143d8d900de896318233a942fb356c9afe00946bda357d894a5cf7b4  third_party/superstack/vectors/seed.json
e59115d5ca45df9389ccdc03c9a5e65e33086da3f4e0814e0e20dde7a35d11fc  third_party/superstack/vectors/sound.json
b3afebfca166385ff54f518b382ffa9f53846e46c9b9716cbd9af5f8c83698e4  src/renderer/aces2.hpp
027256868ee97b90576bcca16418588ef9b3e09717d010ff6ff3ea83e28d1530  src/renderer/aces2_cam.cpp
7c61ba268d572154a59cd4fecddae9a059717eef8506b5fda6aba7e8cfbc6368  src/renderer/aces2_gamut.cpp
31fb6707dac9500057f6de5e72dfbbf994b27675ec18e2949b582a1207cf917f  src/renderer/aces2_tables.cpp
1a1ce3dbca16df2f1405c8578f6de6bf8de671d3bee393665ffdcc3549f7c711  web/colour/colour.wgsl
e319da67ce990a6d163411db1ef81641fbe76b9047f98575e9067280f69cb6bd  docs/motion/hanken-grotesk-500.atlas.json
```
