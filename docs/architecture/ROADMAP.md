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
when measured. A6 holds 0.x semantics until M3, where the public API freezes.

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

## M3: Lit scenes, a frozen API, a verified viewer

Scope: shadows, TAA and SMAA, frame-graph aliasing and history, the validating layer, the Studio viewer (A4). The API freezes here (A6).

Exit criteria:
1. `MetalRoughSpheres`, `DamagedHelmet` and a Sponza-class scene render with GPU verdict `verified` at committed tolerances; the rasterised frame's error against a path-traced reference is recorded per scene with its bound stated first.
2. TAA meets its ghosting and RMSE bounds against the supersampled reference.
3. Transient aliasing lowers peak GPU memory on the corpus by a recorded amount with byte-identical output; the validation layer catches every injected error in its negative-control suite.
4. The Studio opens a glTF file, renders it, and shows the CPU reference, the error map and the certificate (A4).
5. The public C++ API and the plugin C ABI are documented, an API dump is checked in CI, and source compatibility within a major is promised from the first release after M3.

## M4: Scale and reach

Scope: Vulkan (A1), native window, swapchain and HDR output, clustered lighting, GPU culling, LOD and meshlets by compute, skeletal and morph animation, general particles, Tracy zones.

Exit criteria:
1. The identity matrix and the M2 and M3 corpora pass on D3D12, Vulkan and WebGPU, with a golden per device class.
2. 60 fps at 2560 x 1440 on a mid-tier GPU for a Sponza-class scene with PBR, shadows and 64 point lights: p95 at most 16.7 ms over 10,000 frames, on D3D12 and Vulkan (carried from the repository roadmap).
3. Skinned and morphed glTF models match a CPU skinning reference within a stated RMSE.
4. One million particles at a frame time recorded on the reference machine, with seed-determinism across runs.
5. Frames per joule are published for the M1 scenes and the M4 scene.

## M5: Light transport and breadth

Scope: global illumination, screen-space reflections, an own temporal upscaler, Metal (A1), USD import if asked, packaging and crash reporting (sentry-native, MIT), and the review of A3.

Exit criteria:
1. A GI technique, written from its paper, matches a converged path-traced reference on a small scene within a bound stated first, and its cost is recorded.
2. Metal passes the identity matrix.
3. Installers, an SBOM and licence texts ship in the release archive; a crash produces a minidump with a build ID.
4. A written review of A3 uses evidence from M2 to M4: did users ask for a runtime?
