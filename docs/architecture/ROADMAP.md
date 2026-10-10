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
3. **HDR.** The extended-range path works on a browser that offers it, and `evidence/m1-hdr-patch.json` holds a calibrate-pro measurement of a 1,000-nit patch within 5%.
4. **Vector.** Stroke dashes, joins and caps, gradients, clip paths and image fills pass a corpus listed in `evidence/m1-vector-corpus.json` before the first run, each within the stated coverage error of the CPU reference. Node tests cover flattening, dash and join geometry.
5. **Text and maths.** Every equation in films 1 to 5 renders from the engine's TeX subset or the documented MathJax path with no manual patch, checked by a script that lists the unsupported macros (expected: none).
6. **Media output.** The manifest of each scene records an encoder probe for the machine. Two runs of `ao-check` on the same device class produce identical frame hashes for every frame. An offline audio mix matches the `superstack.sound/1` vectors sample for sample. The CPU-adapter smoke job stays inside a wall-clock budget stated in `media.json`.
7. **Budgets.** Media stats files hold per-pass GPU ms where the adapter has timestamps, and a budget gate fails a local run that exceeds its file. A baseline of joules per frame exists for both media scenes, or the evidence file says the telemetry was unavailable.
8. **Records.** ADR 0013 records this roadmap (if the author accepts it), and `GAP-ANALYSIS.md` cites it.

9. **Display chain.** The CRT pass (scanlines, phosphor masks and persistence, halation, curvature, bezel, colour separation, vignette), film grain and chromatic aberration run as WGSL passes in the Motion finish and as a Studio layer, each with a CPU reference. The site's `retro-crt.js` and the shader agent's references serve. On a committed frame set at 960 x 600 with fixed seeds, GPU output is within 1/255 at 8-bit of the reference. The set and the script are committed before the first run. A showcase scene uses them.
10. **Palette and dither.** Bayer 2, 4 and 8, interleaved gradient noise and a blue-noise mode quantise to a palette in OKLab. The ordered modes are bit-equal to `retro-dither.js`. The blue-noise texture comes from the MIT Atrix256 generator, vendored with its hash and a manifest entry.

Likely failure points: browser encoders that refuse 4K; hardware encoders that are not bit-exact (frame hashes come from raw readback, not from the encoded file); CI runners without timestamps.

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
