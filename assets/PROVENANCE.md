# Provenance of the owned assets

raw-native's M3 scenes use only assets this repository generates. The author decided this on
2026-10-10: "No test models, only full owned assets." No third-party geometry, texture, scan or
reference image went into any of them.

| Asset | What it is | Source | Licence |
|---|---|---|---|
| `raw-hero` | A helmet-class hero asset. Its parts: a clearcoated, brushed (anisotropic) metal shell; a sheen fabric liner; a transmissive, volumetric, iridescent visor; rubber seals with IOR and specular; a plain gold trim; plain dielectric vents; emissive indicator strips. It carries every material extension the engine models. | `src/tools/owned_hero.cpp` | FSL-1.1-MIT, as the engine |
| `raw-hall` | An interior hall at Sponza scale: 37 x 14 x 17 m, 586,424 triangles. It has 52 fluted columns on two storeys, 48 round arches, a gallery, an open atrium, a tiled floor, 8 two-sided sheen drapery panels and 10 emissive lanterns. | `src/tools/owned_hall.cpp` | FSL-1.1-MIT, as the engine |
| `raw-spheres` | The material gallery's 49 spheres, seven material families. | `raw/renderer/gallery.hpp` | FSL-1.1-MIT, as the engine |

## The code is the asset

The generators use deterministic arithmetic: double precision with `+ - * /` and `sqrt`, a
fixed-series sine and cosine (`owned::dsin`, `owned::dcos`), and one rounding to float. The same
code therefore writes the same bytes on every compiler. To write the files:

```
raw_native_cli export-assets --out DIR [--preview]
```

This writes `raw-hero.gltf` and `raw-hall.gltf` with their `.bin` buffers. `--preview` adds quick
Lambert-shaded PNG views. The SHA-256 of each file is committed in `evidence/m3-assets.json` and in
`evidence/m3-scene-models.json`.

`test_owned_assets` regenerates both assets on every CI compiler and checks the hashes. It also
checks the geometry, the scale, the hero's material coverage, the hall's content and the glTF round
trip (bounds: `evidence/m3-assets-bounds.json`).

## Why the binaries are not committed

The hall's buffer is 15 MB and the hero's 2.7 MB. Committing them would grow the history on every change to the
generator, and it would add a second copy of something the code already defines exactly. The
hashes pin the bytes, and anyone can regenerate them. If the author wants the files in the
repository anyway, `export-assets` writes them in place.
