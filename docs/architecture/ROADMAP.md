# Roadmap to a consumer-quality renderer

Milestones in order, each with exit criteria a stranger can check. There are
no dates: a milestone is done when its criteria pass, and its evidence is
committed under `evidence/`. Every milestone keeps the four properties in
[ARCHITECTURE.md](ARCHITECTURE.md#what-stays-true-at-every-scale), and every
milestone's features land in the CPU reference first or with their fast path.

Each competitor's lead from [GAP-ANALYSIS.md](GAP-ANALYSIS.md) appears below as
a target in the milestone that closes it. Nothing is ceded; what is deferred is
scheduled.

"Mid-tier GPU" means the class one generation behind the current mainstream,
named in the evidence file when measured (an RTX 4060 or RX 7600 class card in
2026 **[memory, unverified]**). "Reference machine" means the author's RTX 4090
workstation.

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

## M1: Scenes from files

- RHI: sampled textures, samplers, texture uploads; graph resources for
  textures.
- `assets` layer: glTF 2.0 import (meshes, node transforms, metallic-roughness
  material factors and base-colour textures), PNG decode; `superstack.scene/1`
  import and export.
- A `render` CLI verb that takes a glTF file.
- First vendored dependencies, if the author accepts ADR 0005, each with its
  manifest entry.

**Exit criteria**

1. Ten named models from the Khronos glTF sample assets (listed in
   `evidence/m1-corpus.json` before the first run) import without error and
   render on the CPU reference, D3D12 and WebGPU.
2. Every GPU render of the corpus has a `raw-gpu-cert/1` verdict of `verified`
   against tolerances committed before the first textured GPU run.
3. The corpus's CPU outputs are added to the golden manifest and are
   byte-identical on MSVC and GCC.
4. The glTF parser survives one million fuzzed inputs without a crash, hang or
   sanitizer report.
5. **Closes:** Unity's and bgfx's lead on asset intake, for glTF.

## M2: Physically based shading

- Metallic-roughness BRDF following Filament's published model (GGX, Smith
  height-correlated visibility, Schlick Fresnel, Lambert diffuse), photometric
  light units and a physical camera.
- Image-based lighting with prefiltered environment maps; punctual lights
  (directional, point, spot); shadow maps; exposure and tone mapping; the HDR
  channel kept unclamped.
- A CPU reference for each, including a converged path-traced reference for
  lighting at small sizes.
- Material permutations; the shader-language decision (A5) is made here.

**Exit criteria**

1. A white-furnace test on the CPU reference returns the input radiance within
   1e-3 for every roughness in 0.05 steps.
2. `MetalRoughSpheres`, `DamagedHelmet` and `Sponza` from the sample assets
   render with GPU verdict `verified` on D3D12 and WebGPU at committed
   tolerances.
3. The rasterized PBR frame's error against the path-traced reference is
   recorded per scene in a certificate, with its bound stated before the run.
4. **Closes:** Filament's lead on a documented PBR model; Godot's on standard
   lighting features (clustered lighting itself lands in M4).

## M3: A window and a second native backend

- `platform` layer: a window, input, a swapchain surface, HDR output (scRGB
  FP16 by default, HDR10 as an option), frame pacing.
- Vulkan backend (or the first backend the author picks under A1).
- RHI: graphics pipelines, render passes, timestamp queries, a validating
  layer for debug and CI builds.
- Frame graph: transient aliasing, history resources.
- The verified viewer (A4): open a glTF file, render it, show the CPU
  reference, the error map and the certificate.

**Exit criteria**

1. The identity matrix and the M1 and M2 corpora pass on D3D12, Vulkan and
   WebGPU, with a golden per device class for the reference machine.
2. The viewer presents at the display's refresh rate with no dropped frame over
   10,000 frames of `Sponza` on the reference machine, measured by
   presentation statistics.
3. HDR output is measured on an HDR display: a 1,000-nit test patch reads
   within 5% on a colorimeter.
4. Transient aliasing lowers peak GPU memory on `Sponza` by an amount recorded
   in evidence, with byte-identical output.
5. **Closes:** wgpu's lead on validation; Unreal's and Unity's on graph
   aliasing; bgfx's on backend count (partly).

## M4: Performance

- GPU timestamps per pass in every certificate; Tracy zones.
- Clustered lighting, GPU frustum culling, async compute where a backend has a
  second queue.
- A job system for CPU work and parallel command recording.

**Exit criteria**

1. 60 fps at 2560 x 1440 on a mid-tier GPU for `Sponza` with PBR, shadows and
   64 point lights: p95 frame time at most 16.7 ms over 10,000 frames, on D3D12
   and on the second native backend.
2. The same scene on the reference machine at p95 at most 4 ms.
3. GPU verdicts stay `verified`; the CPU reference certifies a seeded tile set
   of one frame in every 600 during the timed run.
4. **Closes:** The Forge's lead on measured performance; Godot's on clustered
   lighting.

## M5: Consumer release

- Packaging: installers, code signing, an SBOM, crash reporting with
  minidumps (under A2).
- Accessibility for the viewer: screen-reader names for every control, full
  keyboard reach, UI scale from 100 to 200%, reduced motion, colour-blind-safe
  error-map palettes.
- Mesh LODs and texture streaming for scenes larger than GPU memory.
- Metal backend, if A1 keeps macOS in scope.
- The 1.0 API promise (A6) and the C ABI for binary plugins.

**Exit criteria**

1. Installers for every platform in A1 install, run the viewer and uninstall
   cleanly on a fresh machine of each.
2. The accessibility checklist passes with a screen reader on Windows (and
   VoiceOver on macOS if in scope).
3. A scene with twice the reference GPU's memory in textures renders without
   an out-of-memory failure, streaming in under a stated time budget.
4. Every third-party library is in the manifest with its licence text in the
   release archive.
5. **Closes:** Godot's lead on a shipped, accessible application; Unreal's on
   streaming (partly).

## After M5, if the author puts them in scope (A3, A4)

- **Engine runtime:** ECS in `world`, physics behind an interface, scripting
  through the C ABI. Closes Bevy's and Godot's lead as engines.
- **Editor:** scene editing on top of the viewer.
