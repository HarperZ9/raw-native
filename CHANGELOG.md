# Changelog

Each release's full notes, with downloads and evidence, are on the
[releases page](https://github.com/HarperZ9/raw-native/releases). Dates are
the day the release was published on GitHub, in UTC.

## Unreleased

- **Cascaded shadow maps** (roadmap M3). Four stable cascades for the sun, with hard, PCF and
  PCSS filtering and screen-space contact shadows, on the CPU, D3D12 and WebGPU.
  - Against BVH ray casts toward the sun, hard shadows disagree on at most 2 of about 36,000
    interior pixels, and PCSS is within 0.006 on average of a 256-ray soft reference.
  - The cascades do not swim: no fixed point moves within its texel across a 48-step camera
    path, where an unsnapped fit moves every point.
  - The GPU lookups and contact march match the CPU on WARP and SwiftShader.
  - Open failure: the GPU shadow maps differ from the CPU rasterizer on 3 texels on WARP and
    about 11 on SwiftShader, outside the committed bound. It stays open until the RTX 4090 run
    is in; `evidence/m3-shadows-runs.json` records each texel's cause.
  - `raw_native_cli shadow-parity` runs the GPU checks.
- **M3 models by role.** `evidence/m3-scene-models.json` lists the criterion's models with their
  licences and git blob hashes. One `use` field per role picks FlightHelmet or DamagedHelmet, and a
  stand-in or Sponza. `--model-role` and `fetch_gltf.py --manifest` read it.
- **Spec-exact material forms behind a flag.** `Material::specExact` gives KHR_materials_clearcoat's
  view-weighted coat and KHR_materials_iridescence's two-harmonic sum, on the CPU and the GPU, so either
  form can be the default once decided.
- **Source-style baked lighting** (roadmap M3, S10). Radiosity normal maps and ambient cubes
  (Valve, SIGGRAPH 2006), baked by a CPU path tracer on a new BVH.
  - Against converged path tracing, a flat normal is within 1.7% (median) and a bumped normal
    within 5.3%; ambient cubes are within 3.6%.
  - The normal maps are energy-preserving: a flat normal reproduces the flat irradiance
    exactly, which the plain form misses by up to 27%.
  - Bakes are identical for any thread count. `raw_native_cli bake-room` renders a test room
    from its lightmaps and by path tracing.
- **RHI version 3 and a hardware G-buffer** (roadmap M3).
  - New: float and depth formats, four colour targets, depth testing, culling, and vertex
    pulling from storage buffers, on D3D12 and WebGPU.
  - The formats round-trip exactly, and the depth test makes draw order irrelevant.
  - On D3D12 WARP the hardware G-buffer matches the CPU rasterizer on every pixel of the
    test scene and two Khronos models, once the reference snaps vertices as the hardware does.
  - On SwiftShader two pixels miss the committed bound, and the evidence records them.

- **Lighting** (roadmap M3).
  - Punctual lights use physical units (lux and candela, after KHR_lights_punctual), with
    clustered assignment on the GPU. No light is ever missing from its cluster, and clustered
    shading equals brute force.
  - Image lighting is a split sum from the material model's own energy tables. It reads a
    seamless, GGX-prefiltered cube along the BRDF lobe's centroid. Against importance-sampled
    integration the median error is 1.6% and the 95th percentile is 16.9%.
  - The GPU matches the float64 reference on D3D12 WARP and on WebGPU (SwiftShader). Run the
    check with `raw_native_cli lighting-parity`.
  - `raw_native_cli material-gallery` renders 49 spheres in seven material families as PNG
    and PFM, from the reference and from the GPU.

- **Physically based materials** (roadmap M3). The glTF 2.0 material model with the ratified
  material extensions (ior, specular, clearcoat, sheen, transmission, volume, anisotropy,
  iridescence, emissive strength), as a float64 reference and as WGSL for the GPU.
  - White furnaces: metal within 2.9e-4, a white base under the dielectric layer within 4.7e-4,
    anisotropic metal within 3.4e-3. No layered combination reflects more than 1 + 3.7e-4.
  - Reciprocity holds to 3.3e-14 over 20,000 random cases, and the volume BTDF obeys
    generalized reciprocity.
  - The GPU path matches the reference on D3D12 WARP and on WebGPU (SwiftShader) at under half
    the committed bound, over 4,096 cases. `raw_native_cli pbr-parity` runs the check.
  - Iridescence sums eight harmonics where the extension text sums two; that removes a 0.32
    colour error on metals at grazing angles against a spectral reference.

- **Sound engine** (`web/sound/`, design in `docs/sound/DESIGN.md`). A voice-first mix
  for films and interactive pages: narration, a generated score bed ducked under the
  voice, nine procedural cue types bound to the scene timeline and panned to the
  marked object, and a mastering chain (EQ, compressor, true-peak limiter) resolved
  to -16 LUFS and at most -1.5 dBTP.
  - One `Mix` class runs offline in Node and live in an AudioWorklet. Chrome's live
    output reconciles with the offline render at most 1 LSB apart; CI checks it.
  - The mixer renders the `superstack.sound/1` example scene to the contract's
    reference PCM bit for bit (roadmap M1, media output).
  - Meters: BS.1770-5 integrated, momentary, short-term and true peak (Annex 2
    interpolator), and EBU Tech 3342 loudness range, tested against the superstack
    vectors and EBU Tech 3341/3342 cases.
  - `third_party/superstack` now also vendors `superstack.mjs` and the example sound
    scene from the v0.2.0 tag, pinned by SHA-256.
- **Sound in the release loop.** A narrated scene's video now carries the sound
  engine's mix: a sheet from the scene's own `sound()` cues (or its chapters), mastered
  and checked, with the sheet, report and receipt in the bundle and a `sound` block in
  the manifest. "Checking the light" and the walkthrough declare their cues.
  `--no-sound` keeps the bare narration.

## 0.6.0 (2026-10-09)

- **Media engine.** `raw-native media render` (`tools/media/`, ADR 0012) renders a
  repo's videos from `docs/media/media.json` at the commit it runs at.
  - Facts are read from the checkout: JSON values, regex matches, command output and
    PGM or PFM images. A fact that cannot be read stops the render.
  - Each scene renders to a video, a poster, captions, an interactive page and a
    manifest of hashes.
  - Walkthroughs (`web/motion/walkthrough.mjs`) record what each command really prints
    and draw it as a guided terminal session.
  - `.github/workflows/media.yml` is reusable. It renders on Chrome's CPU WebGPU
    adapter, since CI has no GPU, and attaches the result to a release.
    `raw-native media attach` replaces those files with the narrated local render.
  - raw-native's own media: "Checking the light", from the AO maps and certificate of
    the release's own render, and a first-run walkthrough.
- **Motion.** A mathematical-animation layer on the web host (`web/motion/`,
  ADR 0011). Scenes are pure functions of time. Bezier and arc paths are drawn
  with analytic coverage on the GPU, binned in 16-pixel bands, and shapes,
  text and equations morph by point correspondence. Glyph atlases come from any
  font you may redistribute (`scripts/glyph_atlas.py`). There is a small TeX
  subset, live plots, GPU particle formations and a camera that flies through
  scale with depth of field. Threads and Worlds composite as layers. One scene
  file plays live in `web/motion/player.mjs`, with scrub, step, chapters,
  parameter sliders and reduced motion. The same file renders frame-exact to
  4K video through `scripts/motion_render.py`, which drives headless Chrome
  and the browser's hardware encoder and muxes narration and score. A demo is
  at `docs/motion/`. Creative media, no certificate.
- **Worlds.** The One Step worlds as small raymarched dioramas you can walk
  around: signed distance fields lit by sun and lamp, with soft shadows,
  ambient occlusion, reflections, fog and film tone
  (`src/renderer/gpu/shaders/worlds.wgsl`, `web/worlds.mjs`). An explorer
  camera (`web/camera.mjs`): wheel-click drag to orbit, wheel to zoom, shift or
  right drag to pan, WASD and Q E to fly, double-click to focus, R to reset,
  touch orbit, pinch and pan, an idle tour. All fifteen worlds, from Komorebi's
  canopy to the Menger ice cathedral of The many. An optional layer of light
  threads runs along each world's surfaces behind a depth test. Web only;
  creative media, no certificate.
- **Web GPU host.** `web/raw-gpu.mjs` runs WGSL compute and render pipelines,
  the frame graph, per-pass GPU timing and canvas presentation on WebGPU, as
  one dependency-free ES module. `web/frame-graph.mjs` follows the C++ graph's
  rules and is tested against its golden output (ADR 0010).
- **Threads.** A creative module: particles flow along the zero set of fifteen
  form fields and leave trails tone-mapped by log density. One WGSL source runs
  on the web host (`web/threads.mjs`) and on D3D12 (`raw_native_cli threads`).
  It has no CPU reference and writes no certificate.
- **Threads compiles per world on the web.** The world code moved into the one
  pass that uses it, and the web host builds the advance pass for the one or
  two worlds on screen, on demand. Time to the first frame in Chrome on D3D12
  went from 24 s to about 0.3 to 1.3 s.
- **Translator.** `scripts/wgsl_to_hlsl.py` translates `fract`, `mix` and
  `atomicAdd` statements, refuses builtins it cannot map faithfully, and no
  longer treats a comparison inside a constructor as a template bracket, which
  had hidden the comma in `vec2f(a, select(b, c, k >= 2))`. The renderer's
  generated HLSL is unchanged.
- **Layered source tree.** Headers and sources move into layer directories
  (`math`, `core`, `cert`, `scene`, `rhi`, `graph`, `renderer`, `tools`), and
  `scripts/check_layers.py` checks in CI that each layer includes only the
  layers allowed for it. The 0.5 include paths (`raw/render.hpp` and the
  others) still work through one-line forwarders, removed one minor release
  later. See `docs/architecture/`.
- **Render hardware interface.** `raw/rhi/rhi.hpp` is the one boundary between
  the renderer and a graphics API. The D3D12 and WebGPU backends implement it;
  the GPU frame (buffers, passes, bindings, read-back) now exists once, in the
  renderer, where each backend used to carry its own copy.
- **Frame graph.** `raw/graph/frame_graph.hpp` validates each frame, culls
  passes whose results nothing reads and places barriers from declared
  accesses. The GPU renderer and the CPU reference both run through it.
  Frame-only GPU renders now skip the passes the frame does not need.
- **Binding layouts from the WGSL.** `scripts/wgsl_to_hlsl.py` writes each
  pass's bindings and their access into `pass_layout.hpp`; the graph's
  barriers and the D3D12 root signatures come from it.
- **Checks.** `RAW_NATIVE_D3D12_DEBUG=1` now fails a submission on any D3D12
  debug-layer error. `scripts/identity_matrix.py` hashes every file of eleven
  cases; CI compares MSVC and GCC against `evidence/identity-golden.json`.
- **Output unchanged.** Every certificate, receipt, channel summary and image
  is byte-identical to 0.5.1's, on every build and backend checked.

## 0.5.1

- **superstack 0.2.0.** The vendored `superstack.hpp` and its vector runner come
  from superstack's v0.2.0 tag, byte for byte. The header's SHA-256 is
  `a0d223c0567c883b73ed74b19d93cb087e213dc14b950ab0f6f04206cb72c590`.
  The vectors are unchanged.
- **The WebAssembly build writes `receipt.json`.** superstack 0.2.0's header
  compiles with libc++, so the wasm build no longer skips the receipt. Its
  bytes equal the receipts from MSVC on Windows and GCC on Linux, and
  `raw_native_cli verify` passes on it. CI requires both.
- **Licence of the vendored header.** superstack 0.2.0 is FSL-1.1-MIT, so
  `third_party/superstack/LICENSE.txt`, shipped in every archive as
  `LICENSE-superstack.txt`, moves from MIT to FSL-1.1-MIT. The notices for
  mulberry32, xmur3, OKLab and libebur128 are kept word for word.
- **CMake 4.2.0 is pinned for the wasm build.** CMake 4.2 adds `-fPIC` to
  Emscripten compiles, so another version gives different wasm bytes.
  `cmake/wasm.cmake` warns on any other version, and CI installs 4.2.0 and
  checks that the wasm build used it.
- **Output unchanged.** `certificate.json`, `channels.json`, the frame and the
  AO files hash the same as in 0.5.0, apart from the certificate's version
  string. CI checks them against 0.5.0's hashes.

## 0.5.0 (2026-10-04)

- A native D3D12 backend for Windows, built with `-DRAW_NATIVE_GPU_D3D12=ON`.
  Its HLSL is generated from the WGSL, so there is one shader source.
- `--gpu` refuses software adapters, including the Microsoft Basic Render
  Driver. `RAW_NATIVE_D3D12_WARP=1` selects WARP on purpose.
- Every render writes `receipt.json`, a `superstack.receipt/1` receipt that
  reports identity and the tolerance verdict separately. A `--gpu` run adds
  `gpu_receipt.json`. `raw_native_cli verify` checks both.
- `gpu_certificate.json` gains `identity`, both frame hashes and
  `adapter.driver`. `certificate.json` is unchanged apart from its version
  string.
- The wasm build writes no receipt: superstack 0.1.0's header does not
  compile with libc++.

## 0.4.0 (2026-10-03)

- A WebGPU backend in WGSL that runs the whole frame as compute passes.
- `--gpu` renders on the GPU and on the CPU, then writes
  `gpu_certificate.json` (`raw-gpu-cert/1`) comparing the two. The bounds in
  `raw/gpu_tolerance.hpp` were committed before any GPU code existed.
- The default native build and the CPU wasm build contain no GPU code. There,
  `--gpu` exits 4 with an `unverifiable` certificate.

## 0.3.0 (2026-10-03)

- Certificate schema `raw-cert/2`, with the renderer version, canonical
  params, sample counts and the SHA-256 of every output file.
- `ao_rt.pfm`, `ao_ss.pfm` and `mask.pgm`, so `raw_native_cli verify` and
  `scripts/recheck.py` can recompute RMSE and maximum error from the files.
- `--tolerance`, `--no-rt`, `--threads` and `--bench`.
- A WebAssembly build and a loader that checks both files against a SHA-256
  before running them.
- The licence changes to FSL-1.1-MIT. Version 0.2.0 and earlier stay MIT.

## 0.2.0 (2026-10-03)

- First public release: ray-traced and screen-space ambient occlusion, a
  reconcile step and `certificate.json` with a `verified` or `refuted` verdict.
- A bounded memory arena, recorded in `arena_certificate.json`.
- Camera and frame-size flags, a JSON params file and a previous camera for
  motion vectors.
- `frame_hdr.pfm` and `channels.json`.
- CMake install rules and a `raw_native` package for `find_package`.
