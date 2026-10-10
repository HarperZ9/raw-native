# Where raw-native leads and trails

The capability comparison behind the milestone plan in [ROADMAP.md](ROADMAP.md),
recorded on 9 October 2026 and accepted by the author the same day (ADR 0013).
It is a snapshot, not a scoreboard: every row names its best reference with the
source it came from, and every target is a test that has not been run until its
evidence lands under `evidence/`. Rows are rechecked against each engine's
then-current release before any claim of parity or lead is made.

The measured references for parity, area by area, are Filament, Bevy, Godot and
Wicked Engine. Source tags: "report 1" to "report 4" are research reports read from
primary pages on 8 and 9 October 2026; `[memory]` marks a fact recalled, not read.
The older comparison of 3 October is [GAP-ANALYSIS.md](GAP-ANALYSIS.md).

## Comparison set

Profiled from primary pages (versions as the reports recorded them):

| Group | Engines |
|---|---|
| Native open renderers | Filament 1.77.3 (Apache-2.0), bgfx (BSD-2-Clause), The Forge 1.63 (Apache-2.0), Diligent Engine (Apache-2.0), Sokol (zlib) |
| Single-author and small-team engines | Wicked Engine (MIT), Overload (MIT), The Machinery (closed down 2022; architecture unknown) |
| Rust and open engines | Bevy 0.20 (MIT OR Apache-2.0), Godot 4.7 (MIT), wgpu v30.0.1 (Apache-2.0 OR MIT), Vello (Apache-2.0 OR MIT) |
| Web | three.js r186 (MIT), Babylon.js 9.30.0 (Apache-2.0), PlayCanvas v2.23.1 (MIT) |
| Media and animation | Remotion (source-available, not OSI), Motion Canvas (MIT), ManimCE 0.22.0 and ManimGL (MIT), Rive runtime (MIT), Lottie-web (MIT), Theatre.js (core Apache-2.0, studio AGPL-3.0) |
| Vector and text | Slug (patent dedicated to the public domain on 2026-03-17; reference shaders free with credit), Vello, Pathfinder, msdfgen (MIT) |

Reasons for the picks: Filament for the documented material and colour model; Wicked as the strongest single-author engine with published feature lists; Bevy and Godot for open feature breadth and published performance numbers; three.js and Babylon.js because the Studio and films run in browsers; Remotion, Motion Canvas and manim because they are the media-layer peers; Slug and Vello because the media layer needs text and vector fidelity.

## The comparison

"raw-native" is the state at commit `19b0887` (0.6.0). "Best reference" is the strongest profiled source for that area. Targets are measurable proposals; none has been run. Rank is the position in the ranked gap list in section 4 (blank means ranked below ten).

| # | Area | raw-native now | Best reference (source tag) | Gap | Target | Cost | Rank |
|---|---|---|---|---|---|---|---|
| 1 | RHI and backends | Compute-only RHI; D3D12, WebGPU, null | Diligent: D3D11/12, GL, Vulkan, Metal, WebGPU; wgpu: Vulkan, Metal, D3D12 first class (reports 2 and 3) | No textures or graphics pipelines in C++; no Vulkan or Metal | Sampled-texture and graphics-pipeline identity tests on D3D12 and WebGPU (M2); Vulkan passes the same matrix (M4) | XL | 8 |
| 2 | Frame graph and resources | Buffers only; JS mirror; no aliasing | Godot barrier graph, Unreal RDG, Babylon.js Frame Graph with texture reuse (report 1; GAP-ANALYSIS) | Textures, aliasing, history, async compute | Peak GPU memory on the M2 corpus falls by a recorded amount with byte-identical output | L | 8 |
| 3 | Shader pipeline and permutations | One WGSL source; subset translator to HLSL | Filament `matc` with variant filters; Forge FSL with hot reload; wgpu Naga (WGSL, SPIR-V, GLSL in; SPIR-V, MSL, HLSL out); Slang v2026.19 | No textures, permutations, specialization constants, hot reload; no SPIR-V or MSL | Textures and specialization in the translator or in Slang, chosen by a measured spike (A5); permutation count and compile time reported | L | 6 |
| 4 | Materials and PBR | Lambert albedo | Filament: Cook-Torrance, clearcoat, anisotropy, sheen, iridescence, subsurface, cloth, physical light units (report 2) | Everything past albedo | White furnace within 1e-3 for roughness in 0.05 steps on the CPU reference; `MetalRoughSpheres` and `DamagedHelmet` verified on D3D12 and WebGPU | XL | 7 |
| 5 | Lighting, shadows, GI | One directional light, SSAO, RT AO | Filament clustered forward and cascaded/VSM/PCSS shadows; Wicked SSGI, voxel GI, surfel GI, DDGI, RT; Bevy Solari with ReSTIR (off by default); Godot SDFGI, VoxelGI | No shadows, IBL, point or spot lights, GI | Shadows and IBL in M2 to M3 with committed RMSE bounds against a CPU reference; clustered lights in M4; GI only after, from the papers (RTXGI's licence forbids reuse of its code path) | XL | 7 |
| 6 | Anti-aliasing and temporal | None for 3D; analytic coverage for vectors | Filament TAA, FXAA, MSAA, FSR dynamic resolution; Bevy FXAA, SMAA, TAA, CAS, DLSS; Godot TAA, FSR2 | No TAA, SMAA, upscaling | TAA built on the existing reprojected motion vectors; ghosting metric and RMSE against a supersampled reference stated before the run; own upscaler later (FSR, XeSS and DLSS are binary-only grants, see licence notes) | M | 9 |
| 7 | Geometry: LOD, meshlets, instancing | None | Bevy meshlets (experimental), mesh shaders in 0.20 (not web); Forge visibility buffer; Godot auto LOD via meshoptimizer; Wicked | No instancing, culling, LOD | meshoptimizer-based LOD and meshlets driven by compute plus indirect draws, because WebGPU has no mesh shaders (report 4); culling parity with a CPU reference | L | |
| 8 | Animation | 2D morph and easing in Motion; no 3D | Wicked skeletal, morph, IK, retargeting; Forge Ozz skinning; Godot, Bevy glTF skinning | No skeletal or morph 3D; no curve editor model | glTF skinning and morph targets, GPU result within a stated RMSE of a CPU skinning reference; Motion tracks gain cubic Bezier curves | M to L | |
| 9 | Particles and VFX | Fixed-count formations (Motion), Threads flow | Godot GPUParticles3D at hundreds of thousands; Wicked GPU particles, FFT ocean, SPH; Forge particles | No general emitters, forces, collision, sorting, textured sprites, trails | Emitter system with seed-determinism; 1M particles at a recorded frame time on the reference machine | M | |
| 10 | Post-processing and colour | Bloom, vignette, grain, Worlds film tone; HDR rgba16float target | Filament: AgX, PBR Neutral, ACES, GT7, filmic, ASC CDL; Bevy: AgX, TonyMcMapface, PBR Neutral, auto-exposure, DoF, motion blur; Godot HDR output on several platforms; OpenColorIO 2.6.0 (BSD-3-Clause) as an oracle | No display transform, no tone-map set, no HDR output path, no colour management; the author's colour science is unused | Tone mappers and display transforms with a C++ reference checked against OCIO; extended-range canvas output; the HDR signal checked against the published PQ and tone-map formulas, in software | M | 2 |
| 11 | Text and vector | Analytic Bezier coverage in bands, glyph atlases, TeX subset, morphs | Slug (no atlas, patent public domain from 2026-03-17); Vello (CPU renderer mature, compute experimental); Rive and Lottie runtimes | Constant-width strokes only; no dashes, join or cap choices, gradients, clipping or image fills found (confirm in shaders); SVG import; shaping beyond kerning; full TeX | Named SVG corpus within a stated coverage error of a CPU exact-area reference; every equation in films 1 to 5 renders without MathJax | L | 3 |
| 12 | Video and audio output | WebCodecs to ffmpeg mux; 4K median 26.5 ms a frame; narration and score mux | Remotion frame-pure model; Motion Canvas exports image sequences only; WebCodecs in Chrome 94+, Firefox 130+, Safari 26 (4K untested) | No encoder capability matrix, no audio render in the engine, no alpha or image-sequence export, GPU frame determinism untested | Capability probe table for 1080p and 4K per browser; frame hashes stable across runs on one device class; sample-exact offline audio mix against `superstack.sound/1` vectors | M | 4 |
| 13 | Asset pipeline and formats | None (built-in scene); glyph atlas script | Filament glTF 2.0 with many KHR extensions, KTX, Basis; Godot glTF (no USD); Diligent Hydra/USD delegate; Bevy glTF without Draco, sheen, iridescence | No importer of any format | Ten named Khronos sample models import and render on three paths; one million fuzzed inputs without a crash | L | 5 |
| 14 | Determinism and testing | CPU oracle, certificates, identity matrix, independent checker | No profiled peer states an equivalent (sources read) | Coverage is one effect on one scene; every new feature needs its reference | Reference coverage reported per release as features with a reference over features shipped; no feature ships `verified` without one | ongoing | |
| 15 | Profiling and debugging | Web per-pass timestamps; D3D12 debug names and debug layer | Forge Micro Profiler and GPU breadcrumbs; bgfx RenderDoc capture; Tracy v0.14.1 (BSD-3-Clause) | No native timestamps, no profiler zones, no capture hooks, no budgets | Per-pass GPU ms in every certificate and media stats file; a CI budget gate on the reference machine; PIX and RenderDoc markers | S to M | 10 |
| 16 | Editor and tooling | CLI, verifier, media CLI; Studio sits outside | Godot full editor; Wicked editor; Overload editor; Babylon.js Inspector and Node Render Graph editor | A viewer that shows the frame, the reference, the error map and the certificate | The Studio hosts the verified viewer (A4); an editor waits on A3 | L | |
| 17 | Platform reach | Windows, Linux and web by build; browsers with WebGPU | Filament mobile and web; Godot Compatibility path for web (no compute); three.js WebGL2 fallback | No window or swapchain; no mobile build; web needs WebGPU (Chromium 113+, Firefox 141+ Windows, Safari 26) | Browser matrix run on every release for the media scenes; native window and HDR swapchain in M4 | M to L | |
| 18 | Performance per watt | Not measured | Bevy publishes frame times (many_cubes 49.47 ms to 18.77 ms on a mobile RTX 4090, 0.18 to 0.19); no profiled engine publishes energy per frame | No measurement method | Joules per frame and frames per joule for the M1 scenes, from vendor power telemetry; method written first (not verified, see risks) | S to M | 10 |
| 19 | Splats and volumetric media | Studio's Splat Lab defers | Babylon.js and PlayCanvas: Gaussian splats with LOD streaming and GPU sort | No splat renderer in the engine | Splat renderer on the web host after textures (Studio slice A6 draws splats today by its own code) | L | |
| 20 | Audio runtime and physics | None | Godot, Wicked; Jolt v5.6.0 (MIT) physics, miniaudio (MIT-0 or Unlicense) | Out of scope under A3 | Revisit at M5 only if users ask (ADR 0009) | XL | |

## Ranked gaps, top ten

**1. Studio host: textures, samplers, 2D compositing and a shared device.**
Why first: the Studio plan's slice C0 and the twelve tool migrations after it (C1 to C12) wait on it. The web host has render pipelines and Motion already uses textures internally (`web/motion/renderer.mjs` creates textures and a sampler), so this is surfacing and testing an existing capability behind `web/raw-gpu.mjs`.
Target: `host.texture()`, samplers, a pass that takes a 2D canvas as a texture, and `host.share()`. Pixel equality within 1/255 against a CPU composite on SwiftShader and on the RTX 4090. Compositing a 1920 x 1080 canvas costs at most 1.0 ms of GPU time on the reference machine (a proposed bound, to be fixed before the run). One `requestDevice` call across two consumers. A run without WebGPU draws through the fallback.
Cost: M. Owner: the Studio's C0 pull request, which this roadmap does not duplicate.

**2. Colour pipeline and HDR output.**
Why: films and the Studio need a defensible look and correct output on wide-gamut and HDR displays. Filament, Bevy and Godot all ship tone-mapper sets, so parity is table stakes. The author's colour-science work is the one asset no peer has.
Target: Khronos PBR Neutral, AgX and ACES 2.0 tone mappers, sRGB, Display P3 and Rec.2020 output transforms, in WGSL with a dependency-free C++ reference. OpenColorIO 2.6.0 runs only as an offline oracle in a test script, never in the reference path. A bound on colour difference against OCIO is written before the first run. The extended tone-mapping canvas mode is used where the browser offers it. The HDR signal (PQ encode, the extended sRGB curve and the HDR tone map on the neutral axis) is checked in software against a float64 reference of the published formulas, and the GPU encode against the CPU encode. That checks the signal, not what a panel emits (M1 criterion 3, amended 2026-10-10).
Cost: M.

**3. Vector, text and maths fidelity.**
Why: every explainer is text, equations, plots and shapes. Gaps here show on screen. `web/motion/vector.mjs` draws strokes of constant width from the exact distance to the outline, and items take a `blend` mode. I found no dashes, join or cap choices, gradients or clip paths in `vector.mjs` or `path.mjs`; confirm against the shaders before scoping. Update, 10 October 2026: dashes, joins and caps, gradients, clip paths and image fills landed in M1 (`evidence/m1-vector-corpus.json`); the SVG importer, shaping and the wider TeX subset remain.
Target: stroke dashes, join and cap choices, linear and radial gradients, clip paths, image fills; an SVG subset importer; shaping through HarfBuzz at atlas time (offline script, not in the engine); a broader TeX subset. Evidence: a corpus of SVG cases listed in a committed file before the first run, each within a stated coverage error of a CPU exact-area reference; every equation in films 1 to 5 renders without MathJax. Slug's algorithm is now free of patent claims (dedicated to the public domain on 2026-03-17 per the report) and its reference shaders are usable with credit; Vello is a design peer. The current band approach stays unless a measured case argues for a change (ADR 0011's reversal signal).
Cost: L.

**4. Media output you can trust at every release.**
Why: the media engine now runs at each release. The loop is only as good as its weakest encoder or its first nondeterministic frame.
Target: a capability probe table for Chrome, Firefox and Safari at 1080p and 4K, written to the manifest; frame hashes identical across two runs on one device class for the `ao-check` scene (GPU determinism is scoped to a device class, as in ADR 0006); image-sequence export (PNG, and EXR once a writer is vendored); an offline audio mix that matches `superstack.sound/1` vectors sample for sample; the CPU-adapter CI scene finishes inside a stated wall-clock budget. Keep ffmpeg an external tool (see licence notes).
Cost: M.

**5. Asset intake: glTF 2.0, PNG, JPEG, KTX2.**
Why: no importer exists. Viewer, PBR and any 3D film need models and textures.
Target (carried from the repository roadmap, M1 there): ten named Khronos sample models, listed before the first run, import and render on the CPU reference, D3D12 and WebGPU; GPU verdicts `verified` at committed tolerances; CPU outputs added to the golden manifest; one million fuzzed glTF inputs with no crash, hang or sanitizer report. Libraries behind engine-owned adapters: cgltf (MIT, v1.15) and stb (MIT or Unlicense); later libktx (Apache-2.0, v4.4.2) and Basis Universal (Apache-2.0).
Cost: L.

**6. Shader pipeline: permutations, textures and the A5 decision.**
Why: A5 keeps the WGSL subset "until textures and permutations". The web host can run textured WGSL as written, so the translator becomes the constraint only when the native path needs textures (M2). A measured spike decides.
Target: a written comparison of two routes, with the same three shaders through each: grow `scripts/wgsl_to_hlsl.py`, or adopt Slang (Apache-2.0 WITH LLVM-exception, v2026.19). Criteria: lines of translator code, CI proof that outputs match, permutation compile time, targets reached (DXIL, SPIR-V, MSL). Naga can serve as a differential check on HLSL output without being a build dependency. Specialization constants and a pipeline cache follow.
Cost: L.

**7. PBR materials, image-based lighting, punctual lights, shadows.**
Why: this is the 3D look every peer has. It matters to films that show lit 3D and to the viewer.
Target: metallic-roughness BRDF following Filament's published model; prefiltered environment lighting with a DFG table; point and spot lights; cascaded shadow maps. For each, a CPU reference lands first or with the fast path (importance-sampled IBL, brute-force light lists, CPU ray-cast shadow visibility). White furnace within 1e-3 for every roughness in 0.05 steps. `MetalRoughSpheres` and `DamagedHelmet` verdicts `verified` on D3D12 and WebGPU at tolerances committed first. The glTF extensions ratified for materials (clearcoat, transmission, sheen, anisotropy, volume, emissive_strength, iridescence, dispersion) arrive one at a time after the base model.
Cost: XL, spread over M2 and M3.

**8. C++ RHI parity: textures, graphics pipelines, frame-graph textures and aliasing, a validating layer.**
Why: the C++ side is compute-only while the web host is already richer. The certificate path and the D3D12 build need to catch up before PBR can be verified natively.
Target: sampled textures and render passes on D3D12 with an identity test against a CPU sampler reference; graph textures, transient aliasing and history resources; a validation layer with injected-error negative controls (as the dropped transition was for the debug layer); peak GPU memory on the M2 corpus lowered by an amount recorded in evidence with byte-identical output. A single golden test keeps the C++ and JavaScript graph rules equal.
Cost: XL.

**9. Anti-aliasing, temporal accumulation and the post stack.**
Why: geometry edges and shimmer are visible quickly once 3D scenes appear. The reference already produces motion vectors (`src/renderer/motion.cpp`), which is unusual groundwork.
Target: TAA with a ghosting metric and RMSE against a supersampled reference, bounds fixed before the run; SMAA or FXAA as the non-temporal option; depth of field and motion blur on the HDR target. Not proposed: FSR, XeSS or DLSS (binary-only grants, see licence notes).
Cost: M to L.

**10. Profiling, budgets and performance per watt.**
Why: "optimized" needs numbers beside every release. The web host measures per-pass time already; native and the media stats do not.
Target: per-pass GPU milliseconds in every certificate and media stats file where timestamps exist; a budget file per scene with a failing CI gate on the reference machine; PIX and RenderDoc markers; optional Tracy zones (BSD-3-Clause, v0.14.1) behind a flag; a written method for joules per frame from vendor power telemetry (not verified; the report found no peer publishing energy per frame), then a baseline for the two media scenes.
Cost: S to M.

## Next ten

11. 3D animation: glTF skinning, morph targets, cubic Bezier tracks. 12. General particles and VFX. 13. Geometry: LOD, meshlets, culling through compute and indirect draws. 14. Screen-space reflections and contact shadows. 15. Clustered lighting and GPU culling for scale. 16. The verified viewer in the Studio (A4). 17. Native window, swapchain and HDR output (needs Vulkan or D3D12 surface work). 18. Vulkan, then Metal (A1). 19. Global illumination from the papers (DDGI, radiance cascades) with a path-traced CPU reference at small sizes. 20. Gaussian splat rendering; USD import (TinyUSDZ, Apache-2.0) if customers ask.

Ranks 11 to 20 follow dependencies, not value alone: they all need gaps 1, 5, 7 or 8 first.
