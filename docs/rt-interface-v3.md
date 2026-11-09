# Path tracer interface v3: announcement and design

Written 2026-10-21. This note is the announcement that `raw/renderer/rt_pathtrace.hpp` requires
before an interface change merges. The header asks for the announcement in the RT session's
`PROGRESS.md`. No RT session is running, so the announcement lives here in the repository, dated,
where both consumers (the hardware session that trains learned ray reconstruction, and the shader
lab that stylises from the buffers) read it. The header's rule now points at this file.

## Why the version moves

M3 criterion 1 asks for each scene's rasterised error against a path-traced reference. raw-hero
and raw-hall have one (`evidence/m3-verified-scenes-runs.json`, `evidence/m3-lighting-v2-runs.json`).
The material gallery does not, because interface v2 cannot describe it:

- v2 lights a scene with a delta sun, a constant sky and emissive triangles. The gallery uses a
  spot light, a point light and a directional light in physical units, plus the procedural sky
  cube with a 3-degree sun disk.
- v2 has one material model, Lambert plus GGX in metal-roughness form. The gallery's seven rows
  use five families v2 lacks: clearcoat, sheen, anisotropy, iridescence and transmission (thin
  and volume).

v3 adds what the gallery needs and nothing else. ReSTIR DI (ROADMAP proposal P1) later resamples
the same punctual lights.

## The compatibility promise

Interface v4 ended this promise on 2026-10-31 to fix the lobe's energy and the triangle test
(`docs/rt-interface-v4.md`). The text below describes v3.

**Every v2 scene renders byte for byte as it did under v2, on every backend.** A v2 scene leaves
all v3 fields empty, and the tracer takes the v2 code path unchanged. Only the version number in
each output and receipt changes, from 2 to 3. `tests/test_rt_pt_v3.cpp` checks the promise: the
hashes of every owned PT scene's outputs, recorded on main before the v3 code, must match.

## What v3 adds

All of it is in `PtScene`. Each field is empty by default.

| Field | Meaning |
|---|---|
| `lights` | `lighting::Light` punctual lights (KHR_lights_punctual units: lux, candela). Next-event estimation evaluates each light with `lighting::illuminance` and casts a shadow ray. Delta lights take no MIS weight. |
| `environment` | `lighting::Cube`. Level 0 is radiance in nits, read bilinearly with `Cube::sample(0, dir)`. When it is set, it replaces `sky` on escape. `finishPtScene` builds a luminance x solid-angle CDF over its texels. Next-event estimation draws one environment sample a vertex, MIS-weighted by the power heuristic against BSDF sampling, the same pairing v2 uses for emitters. |
| `fullMaterials` | `pbr::Material`, one per texture id, evaluated in float64 by `pbr::evalTerms`: the whole glTF model of `raw/renderer/pbr.hpp`. When the vector is non-empty it replaces `materials` for shading. The base colour is the material's `baseColor` times the texel / 255. Emission is `pbr::emission`. |
| `tables` | `pbr::Tables`, built by `finishPtScene` when `fullMaterials` is set. |

Rules for full materials:

- **Shading frame.** The shading normal is interpolated from the mesh. The tangent is
  `normalize(cross(n, +Y))`, or `+X` where that is degenerate. That is the gallery's latitude
  tangent, and the anisotropy direction is measured from it.
- **Sampling.** One-sample mixture: GGX VNDF reflection at the base roughness, VNDF reflection at
  the coat roughness when there is a coat, cosine on the upper hemisphere, and for transmission
  the mirrored VNDF lobe (thin-walled) or a VNDF refraction (volume), plus a cosine lower lobe.
  The throughput uses the mixture pdf, the sum over strategies, so every strategy stays unbiased
  wherever the mixture covers the BSDF's support.
- **Volumes.** A hit is inside when the ray meets the triangle's authored (counter-clockwise) face
  from behind. Inside a `volume` material the interface takes the inverted index of refraction.
  Beer-Lambert attenuation applies along the traced segment inside, and the material's own
  thickness term is switched off, so the traced path is never attenuated twice. Thin-walled
  transmission keeps the model's mirrored lobe at each crossing.
- **Split radiance AOVs.** The `diffuse` AOV takes `Terms::smooth`: diffuse, sheen and multiple
  scattering. `specular` takes the rest: specular, coat and transmission. The v2 identity
  `emission + diffuse + specular = radiance` still holds.

New control (false by default): `PtControls::envMisWeightOne` weights both environment strategies
1, which double-counts the environment. It is the false control for the environment checks.

## Backends

v3 is CPU-first. `pathTraceCpu` implements it. `pathTraceGpu` returns an error for a scene that sets
any v3 field. A v2 scene on the GPU is untouched, and its parity checks are unchanged. A GPU v3 is a
later step with its own bounds.

## Version bump rationale

`kPathTraceVersion` moves from 2 to 3 because the scene contract grew (new inputs with new
semantics), even though every v2 input gives the same outputs. A consumer that reads `version`
learns that a scene may now carry lights, an environment and full materials. A consumer that only
builds v2 scenes needs no change.

## Checks

Bounds: `evidence/rt-v3-bounds.json`, committed before the code. Tests: `tests/test_rt_pt_v3.cpp`.
The gallery's path-traced error, the reason for v3, is recorded in
`evidence/m3-gallery-pt-runs.json`.
