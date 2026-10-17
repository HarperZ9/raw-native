# Roadmap to a media-grade, professional-grade engine

Milestones in order, each with exit criteria a stranger can check. There are no
dates: a milestone is done when its criteria pass and their evidence is committed
under `evidence/`. Every milestone keeps the four properties in
[ARCHITECTURE.md](ARCHITECTURE.md#what-stays-true-at-every-scale): a CPU reference
for every verified feature, certificates on every run, byte-identical determinism
where the platform allows it, and visible dependencies. Creative modules (Threads,
Worlds, Motion) keep their ruling: no certificate.

This plan replaced the earlier milestone list on 9 October 2026, when the author
accepted the capability roadmap and its five decisions (ADR 0013). The ranking puts
the customers first: the explainer films, the Studio, and the media every release
renders. So the colour pipeline, vector and text fidelity and dependable media output
come before a second native backend, and Vulkan and the native window move to M4.
The comparison behind the order, with where raw-native leads and trails each
measured reference, is [COMPARISON.md](COMPARISON.md).

"Reference machine" means the author's RTX 4090 workstation. "Mid-tier GPU" means
the class one generation behind the current mainstream, named in the evidence file
when measured. A6 holds 0.x semantics until M3, where the renderer and scene APIs get
a versioned snapshot; the full freeze waits for a complete engine (ADR 0015).

**Revision of 9 October 2026 (ADRs 0014 to 0016).**
- **Styles and genres pillar.** The author's direction for raw-native is "an upgraded
  modern engine with retro features": scanlines, CRT, phosphors, film, halation, grain,
  pixel art, and painted and illustrated looks. The named references are Disco Elysium,
  Zero Parades, Half-Life, Kingdom Hearts and isometric RPGs.
- **Style passes.** Each is a first-class pass with a CPU reference where one exists.
  The display chain and the palette and dither join M1 as criteria 9 and 10.
- **The original shader library.** A separate shader agent owns it (CRT, film, retro
  3D, pixel art, painterly). Its passes plug into the M1 post stack; nothing here
  writes a parallel version.
- **Game runtime.** It is in scope, staged G0 (M2) to G4 (M6), with M1 untouched
  (ADR 0014).
- **New milestone.** M6, Authoring, is added.
- **Templates.** The three starter templates are M2's style acceptance corpus
  (ADR 0016).

## M0: Foundation (landed with the architecture change)

- Layered source tree with include rules checked in CI.
- RHI over D3D12, WebGPU and null, extracted from the duplicated backends.
- Frame graph for the GPU renderer and the CPU reference.
- Golden identity matrix in CI; D3D12 debug layer as a failing check.

**Exit criteria**

1. Every certificate, receipt and image is byte-identical before and after, on
   MSVC, GCC, WebAssembly, D3D12 on WARP, WebGPU on SwiftShader, and D3D12 and
   WebGPU on the reference machine.
2. `check_layers.py` passes with its self-test in CI.


## M1: A media-grade host

Scope: gaps 1, 2, 3, 4 and 10, web first (A1).

Exit criteria:
1. **Studio host.** `web/raw-gpu.mjs` exposes textures, samplers, canvas-as-texture compositing and `host.share()` (the Studio's C0 pull request). A browser test composites a 2D canvas and matches a CPU composite within 1/255 on SwiftShader and on the RTX 4090. The count of `requestDevice` calls is 1 with two consumers. The no-WebGPU fallback draws.
2. **Colour.** The tone mappers and output transforms have a C++ reference and WGSL implementations. Over a committed grid of 33 x 33 x 33 RGB values plus grey and saturation ramps, the GPU result is within 1/255 of the C++ reference at 8-bit output, and the C++ reference is within a colour-difference bound (stated in `evidence/m1-colour-bounds.json` before the first run) of an OCIO 2.6.0 result produced offline. The default build keeps no new dependency.
3. **HDR signal** (amended 2026-10-10; it replaced a measured 1,000-nit patch, by the author's decision). Checked in software only. It checks the encoded signal, not the light leaving a panel. Bounds are in `evidence/m1-hdr-signal-bounds.json`, written before the first run. (a) The PQ (SMPTE ST 2084) encode and decode, the extended sRGB curve and the ACES 2.0 HDR tone map on the neutral axis match a float64 reference of the published formulas (`tools/colour/hdr_signal_ref.py`). (b) The WGSL HDR pipelines, read back from the GPU as float32 over the criterion 2 grid, match the C++ encode: PQ within one 10-bit code, extended sRGB within max(1/1023, 0.5%). This holds on SwiftShader and the RTX 4090. (c) A control with a wrong PQ exponent fails both. The extended-range canvas path keeps its own test (`tests/web/hdr_output.py`).
4. **Vector.** Stroke dashes, joins and caps, gradients, clip paths and image fills pass a corpus listed in `evidence/m1-vector-corpus.json` before the first run, each within the stated coverage error of the CPU reference. Node tests cover flattening, dash and join geometry.
5. **Text and maths.** Every equation in films 1 to 5 renders from the engine's TeX subset or the documented MathJax path with no manual patch, checked by a script that lists the unsupported macros (expected: none).
6. **Media output.** The manifest of each scene records an encoder probe for the machine. Two runs of `ao-check` on the same device class produce identical frame hashes for every frame. An offline audio mix matches the `superstack.sound/1` vectors sample for sample. The CPU-adapter smoke job stays inside a wall-clock budget stated in `media.json`.
7. **Budgets.** Media stats files hold per-pass GPU ms where the adapter has timestamps, and a budget gate fails a local run that exceeds its file. A baseline of joules per frame exists for both media scenes, or the evidence file says the telemetry was unavailable.
8. **Records.** ADR 0013 records this roadmap (if the author accepts it), and `GAP-ANALYSIS.md` cites it.

9. **Display chain.** The CRT pass (scanlines, phosphor masks and persistence, halation, curvature, bezel, colour separation, vignette), film grain and chromatic aberration run as WGSL passes in the Motion finish and as a Studio layer, each with a CPU reference. The site's `retro-crt.js` and the shader agent's references serve. On a committed frame set at 960 x 600 with fixed seeds, GPU output is within 1/255 at 8-bit of the reference. The set and the script are committed before the first run. A showcase scene uses them.
10. **Palette and dither.** Bayer 2, 4 and 8, interleaved gradient noise and a blue-noise mode quantise to a palette in OKLab. The ordered modes are bit-equal to `retro-dither.js`. The blue-noise texture comes from the MIT Atrix256 generator, vendored with its hash and a manifest entry.

Likely failure points: browser encoders that refuse 4K; hardware encoders that are not bit-exact (frame hashes come from raw readback, not from the encoded file); CI runners without timestamps.

Status of the M1 criteria (2026-10-14). **M1 is closed: every row passes.** A row passes only when every check behind it passes on every backend it names. The RTX 4090 runs are of 2026-10-10 at commit 9dcd099.

| Criterion | Status | Evidence |
|---|---|---|
| 1. Studio host | Passes. Host and shared consumer within 1/255 of the CPU composite on SwiftShader (CI) and the RTX 4090 (half-float accumulation rounded once, amended 2026-10-10); one `requestDevice` for two consumers; the no-WebGPU fallback draws | `tests/web/host_textures.py`, `evidence/m1-host-textures-rtx4090.json` |
| 2. Colour | Passes on SwiftShader and the RTX 4090; the C++ reference within its bound of OCIO 2.6.0; no new dependency in the default build | `evidence/m1-colour-bounds.json`, `m1-colour-ocio.json`, `m1-colour-gpu-swiftshader.json`, `m1-colour-gpu-rtx4090.json` |
| 3. HDR signal (amended 2026-10-10) | Passes in software on SwiftShader and the RTX 4090; the wrong-exponent control fails. It checks the signal, not the light a panel emits | `evidence/m1-hdr-signal-bounds.json`, `m1-hdr-signal-swiftshader.json`, `m1-hdr-signal-rtx.json` |
| 4. Vector | Passes the committed corpus on SwiftShader and the RTX 4090 | `evidence/m1-vector-corpus.json`, `m1-vector-swiftshader.json`, `m1-vector-rtx4090.json` |
| 5. Text and maths | Passes: 0 unsupported commands, 0 missing glyphs; a `\sum` control is reported and exits 1 | `evidence/m1-tex-coverage.json` |
| 6. Media output | Passes: each scene's manifest records the machine's encoder probe; two runs of ao-check at 1920 x 1080 on the RTX 4090 give identical hashes for all 1,456 frames (and on SwiftShader in CI); the offline audio mix matches the `superstack.sound/1` vectors; the smoke job stays inside its wall-clock budget | `evidence/m1-frame-determinism-swiftshader.json`, `m1-frame-determinism-rtx4090.json`, `tools/media/audio_mix.py` with `tools/media/test_media.py`, CI `media-determinism` and `media-smoke` |
| 7. Budgets | Passes (2026-10-14). Per-pass GPU ms are in the media stats where the adapter has timestamps. Frame and pass budgets were added to `docs/media/media.json` on 2026-10-14; before that the gate had no frame or pass budget to fail. The RTX 4090 record is within them and an over-budget control fails, in CI. Joules per frame for both scenes are board-reported, not metered | `tools/media/budget.py`, `tests/media/`, `evidence/m1-energy-ao-check-rtx4090.json`, `m1-energy-what-it-draws-rtx4090.json` |
| 8. Records | Passes: ADR 0013 records this roadmap; `GAP-ANALYSIS.md` cites it | `docs/architecture/adr/0013-media-grade-roadmap.md` |
| 9. Display chain | Passes. Engine side: every pass within one code of its CPU reference on SwiftShader and the RTX 4090. Studio layer (live 2026-10-15): the Finish layer on harperz9.github.io/studio.html runs the CRT (classic and PVM-20), the film print with grain and halation, the classic tube's chromatic aberration and the PICO-8 dither from raw-native 0.8.0. On the committed frame set, the layer's own frame function draws into a texture of the canvas format; read back, every channel is within one code of the CPU references for all four looks, and a shifted-reference control fails for each. Checked against the live site on SwiftShader at desktop and phone widths | `evidence/m1-post-passes-swiftshader.json`, `m1-post-passes-rtx4090.json`, `m1-studio-layer-swiftshader.json` |
| 10. Palette and dither | Passes: the ordered modes bit-equal to `retro-dither.js`; the blue-noise generator vendored with its hash | `evidence/m1-post-passes-swiftshader.json`, `m1-post-passes-rtx4090.json` |

## M2: Assets and materials

Scope: gaps 5, 6, 7 (base model, IBL, punctual lights) and the texture half of gap 8.

Exit criteria:
1. Ten named glTF models, listed before the first run, import and render on the CPU reference, D3D12 and WebGPU; GPU verdicts `verified`; CPU outputs in the golden manifest, byte-identical on MSVC, GCC and WebAssembly.
2. One million fuzzed glTF inputs: no crash, hang or sanitizer report.
3. The RHI has textures, samplers, graphics pipelines and render passes on D3D12 and WebGPU; a sampled-texture identity test passes against the CPU sampler reference.
4. The A5 spike report exists with measurements, and the author has decided.
5. White furnace passes within 1e-3 for every roughness in 0.05 steps.
6. `third_party/MANIFEST.md` lists every vendored library with upstream, version, SPDX licence, adapter file and hash; CI verifies the hashes.
7. **Pixel-perfect scaling.** Integer upscale is bit-equal to a nearest-neighbour reference. A camera pan in 0.1-pixel steps moves no edge by more than one output pixel beyond a continuous-pan reference; the metric is written first.
8. **Retro 3D flags.** Vertex snap and affine UVs match their CPU references within a stated bound on a committed mesh set.
9. **Isometric camera and picking.** Picking round-trips every grid cell of a test map. The occlusion fade mask matches its CPU reference.
10. **Sprites and tiles.** 100,000 tiles draw at a recorded frame time, in identical order across runs. The LDtk and TMX loaders survive one million fuzzed inputs.
11. **Outlines and toon ramps.** Depth, normal and ID edge outlines and inverted-hull outlines each have a test scene with an image-difference bound set first.
12. **Style templates (ADR 0016).** Three templates build and pass their checks on D3D12 and WebGPU, ship in the Studio, and pass the author's contact-sheet review:
    - a painted isometric RPG ("more Disco Elysium and less Rogue Trader", abstract, grotesque, very indie);
    - pixel art in 2.5D;
    - a retro-look thriller (scary, not built on jump scares).
13. **G0.** The scene hierarchy round-trips through `superstack.scene/1`.

## M3: Lit scenes, an API snapshot, a verified viewer

Scope: shadows, TAA and SMAA, frame-graph aliasing and history, the validating layer, the Studio viewer (A4), and runtime stage G1 (main loop, input, camera controllers, picking). The renderer and scene APIs get a versioned snapshot here; they keep maturing (ADR 0015).

Exit criteria:
1. `MetalRoughSpheres`, `DamagedHelmet` and a Sponza-class scene render with GPU verdict `verified` at committed tolerances; the rasterised frame's error against a path-traced reference is recorded per scene with its bound stated first.
2. TAA meets its ghosting and RMSE bounds against the supersampled reference.
3. Transient aliasing lowers peak GPU memory on the corpus by a recorded amount with byte-identical output; the validation layer catches every injected error in its negative-control suite.
4. The Studio opens a glTF file, renders it, and shows the CPU reference, the error map and the certificate (A4).
5. The renderer and scene APIs and the plugin C ABI are tagged as a numbered snapshot, documented, with an API dump checked in CI and a compatibility test suite that runs against the snapshot. A later break moves to the next snapshot with a migration note (ADR 0015).

Dated additions (2026-10-10). They add checks and never loosen the five above:

6. **Materials.** The glTF 2.0 material model with KHR_materials_ior, _specular, _clearcoat, _sheen,
   _transmission, _volume, _anisotropy, _iridescence and _emissive_strength has a float64 CPU reference
   (`raw/renderer/pbr.hpp`) that passes white furnaces, conservation and reciprocity, and the WGSL
   path (`pbr.wgsl`) matches it on D3D12 and WebGPU within the bounds in
   `evidence/m3-materials-bounds.json`, with a control that must fail.
7. **Lighting.** Punctual and area lights in physical units, clustered light assignment against a
   brute-force list, and image-based lighting against importance-sampled integration, each with
   bounds committed before its first run.
8. **Shadows, AO, reflections, TAA.** Cascaded and contact shadows against CPU ray-cast visibility;
   GTAO and SSR each against a CPU reference; TAA as in criterion 2, with no binary upscaler.
9. **Baked lighting.** Source-style directional lightmaps (radiosity normal mapping) and ambient cubes
   (Mitchell, McTaggart and Green 2006) as an optional path, baked by the CPU ray tracer, with the
   bake's error against a converged path trace recorded.

Status of the M3 criteria (2026-10-12). A row passes only when every check behind it passes on
every backend it names.

| Criterion | Status | Evidence |
|---|---|---|
| 1. Scenes verified, path-traced error recorded | Open. The owned assets exist and pass their checks (`evidence/m3-assets-runs.json`); the verified renders and path-traced errors are not started | `evidence/m3-assets-bounds.json` |
| 2. TAA ghosting and RMSE | Passes on the CPU: pan RMSE 0.442 of single-sample (bound 0.5), ghosting 0.0056 (bound 0.03), controls fail. GPU parity passes on WARP, SwiftShader and the RTX 4090 (0 pixels, bound 16) | `evidence/m3-post-runs.json` |
| 3. Aliasing and the validation layer | Passes on WARP and lavapipe (2026-10-17): with aliasing on, every corpus output is identical and transient bytes fall 2.80% (all of it in the GPU renderer graph). The validation layer catches all 26 injected errors across its 13 rules and reports nothing on the corpus after it found one real missing barrier, now fixed. Aliasing is off by default | `evidence/m3-aliasing-validation-runs.json` |
| 4. The Studio viewer (A4) | The viewer is built in the engine (`web/viewer/`): it opens a glTF or GLB file, renders it through WebGPU and on the CPU reference, and shows both frames, the error map and the GPU certificate; checked headless in CI on raw-hero and raw-hall (both verified on SwiftShader) with a broken-file control. Since 2026-10-12 it ships as `raw-native-0.7.0-viewer.zip` and runs unchanged as the Studio's Verified viewer source on harperz9.github.io (raw-hero and raw-hall `verified` in Chrome on an RTX 4090, desktop and phone) | `tests/web/viewer.py`, the site's `tests/studio-viewer.cjs` |
| 5. API snapshot | C++ part passes: snapshot 2 (2026-10-12; snapshot 1 was 2026-10-11), 25 renderer and scene headers, dump checked in CI, compatibility suite (`tests/test_api_snapshot.cpp`), migration note `api/snapshot-2/MIGRATION.md`. **Deferred by dated note (2026-10-11 decision, recorded 2026-10-12):** the plugin C ABI part waits for a real plugin consumer; ADR 0007 is not accepted to close the criterion | `api/README.md` |
| 6. Materials | First stage passes on WARP, SwiftShader and the RTX 4090. Second stage passes on the CPU: iridescence against an exact reference, volume path length, and clearcoat energy over white metal (|albedo - 1| worst 0.0112, bound 0.02; excess worst 5.9e-4, ceiling 1e-3) since the anisotropic table became denser and embedded (2026-10-12). GPU material parity passes on WARP and the RTX 4090 | `evidence/m3-materials-*.json`, `evidence/m3-materials2-*.json` |
| 7. Lighting | Passes on WARP, SwiftShader and the RTX 4090 | `evidence/m3-lighting-*.json` |
| 8. Shadows: CPU checks against ray casts | Passes | `evidence/m3-shadows-cpu-run.json` |
| 8. Shadows: GPU lookups and contact march | Passes on WARP and SwiftShader | `evidence/m3-shadows-gpu-*.json` |
| 8. Shadows: GPU shadow map against the CPU rasterizer | Passes on WARP and the RTX 4090 since the cascade depth range encloses the scene and raw-hall's coplanar wall tops were removed (2026-10-11). Thin margin: one raw-hall flute sliver sits at 9.9e-6 against 1e-5. **Open failure** on SwiftShader (4-bit edges; recorded, not gated) | `evidence/m3-shadows-runs.json` |
| 8. GTAO and SSR against ray casts | GTAO passes on the near test scene and raw-hall nave; SSR passes on both test-scene views. **Open failure**: GTAO in raw-hall gallery (mean 0.073, p95 0.634); SSR in raw-hall nave and gallery (68% and 65% correct). The two named SSR candidates were probed on 2026-10-12 and neither meets both bounds (method note 12) | `evidence/m3-post-cpu-run.json` |
| 8. GTAO and SSR GPU parity | Passes on WARP and SwiftShader | `evidence/m3-post-gpu-*.json` |
| 9. Baked lighting | Passes |
| RHI v3 G-buffer identity (the base of 6 to 8) | Passes on the test scene and raw-hero. **Open failure** on raw-hall nave: normal ratio 1.26 on the RTX 4090 (one pixel) and 14.4 on WARP (four), bound 1, all on column-flute slivers whose normal turns tens of degrees within one snap step. SwiftShader fails on 4-bit edges | `evidence/m3-rhi3-runs.json` | `evidence/m3-bake-*.json` |

Licence note on criterion 1 (2026-10-10). `DamagedHelmet` derives from a CC-BY-NC-4.0 model and the
Khronos `Sponza` is under the CRYENGINE agreement, so neither can ship in this repository's evidence
or media. The proposal, for the author's decision, keeps the criterion's difficulty: `FlightHelmet`
(CC0-1.0) in place of `DamagedHelmet`, and an interior scene of Sponza's size under CC0 or CC-BY in
place of `Sponza`, named before its first run.
Until the author decides, `evidence/m3-scene-models.json` lists the candidates by role. Each role's
`use` field picks one (`FlightHelmet` and the `ABeautifulGame` stand-in today), so the decision is a
one-field change. `--model-role` renders by role, and `tools/assets/fetch_gltf.py --manifest` fetches
and verifies by role.

Amendment to criterion 1 (2026-10-10, the author's decision: "No test models, only full owned
assets."). The decision supersedes the note above. Criterion 1 now reads: the material gallery's
spheres, `raw-hero` (a helmet-class asset carrying every material extension) and `raw-hall` (an
interior hall at Sponza scale) render with GPU verdict `verified` at committed tolerances, and the
rasterised frame's error against a path-traced reference is recorded per scene with its bound stated
first. All three are generated by code in this repository under its licence. Every other M3 scene
and check uses them too. No M3 test or CI step reads a third-party model. The model manifest stays and
names the owned assets. The asset checks are in `evidence/m3-assets-bounds.json`, and the provenance
is in `assets/PROVENANCE.md`. The difficulty is kept: the hero carries all nine extensions, and the
hall is at least 250,000 triangles and 30 x 14 x 12 metres.

## M4: Scale and reach

Scope: Vulkan (A1), native window, swapchain and HDR output, clustered lighting, GPU culling, LOD and meshlets by compute, skeletal and morph animation, general particles, Tracy zones, and runtime stage G2 (audio playback, animation state machines, event timelines).

Exit criteria:
1. The identity matrix and the M2 and M3 corpora pass on D3D12, Vulkan and WebGPU, with a golden per device class.
2. 60 fps at 2560 x 1440 on a mid-tier GPU for a Sponza-class scene with PBR, shadows and 64 point lights: p95 at most 16.7 ms over 10,000 frames, on D3D12 and Vulkan (carried from the repository roadmap).
3. Skinned and morphed glTF models match a CPU skinning reference within a stated RMSE.
4. One million particles at a frame time recorded on the reference machine, with seed-determinism across runs.
5. Frames per joule are published for the M1 scenes and the M4 scene.

## M5: Light transport and breadth

Scope: global illumination, screen-space reflections, an own temporal upscaler, Metal (A1), USD import if asked, packaging and crash reporting (sentry-native, MIT), and runtime stage G3 (the `world` layer, physics behind an interface, save and load; the entity model chosen by an ADR at M4).

Exit criteria:
1. A GI technique, written from its paper, matches a converged path-traced reference on a small scene within a bound stated first, and its cost is recorded.
2. Metal passes the identity matrix.
3. Installers, an SBOM and licence texts ship in the release archive; a crash produces a minidump with a build ID.
4. An ADR records the entity model chosen for `world`, with the templates' evidence.

## M6: Authoring

Scope: runtime stage G4 (ADR 0014): `scripting` through the C ABI, an editor on the Studio viewer, and a template gallery.

Exit criteria (proposed; fixed before M6 starts):
1. A script written against the C ABI drives a template's gameplay with no engine rebuild.
2. The editor opens, edits and saves each M2 template, and the saved scene round-trips through `superstack.scene/1`.
3. The template gallery in the Studio builds every template from a clean checkout.
