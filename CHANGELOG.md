# Changelog

Each release's full notes, with downloads and evidence, are on the
[releases page](https://github.com/HarperZ9/raw-native/releases). Dates are
the day the release was published on GitHub, in UTC.

## Unreleased

- **Denoiser inference: mean depth in two passes** (HW H2). The single-workgroup reduction took 2.34 ms at 1080p on the
  RTX 4090; two passes take 0.10 ms, and the v4 frame falls from 14.08 to 11.8 ms (prediction committed before: at most
  0.4 ms and 12.2 ms). Timing-only variants of the feature pass show its aux writes cost about 1.0 ms of 2.49. The 2.0 ms
  budget still fails. Parity on the scalar path is unchanged (2.9e-3).
- **Frame and pass budgets** (roadmap M1, criterion 7). `docs/media/media.json` held only the smoke
  wall-clock budget, so `tools/media/budget.py` could not fail a local GPU run: a stats file with a
  900 ms p95 frame and a 40 ms draw pass read "within budget". The file now sets one frame at 60 Hz
  (16.7 ms p95) for both media scenes and per-pass budgets (draw 4 ms; particles, bloom and finish
  2 ms) on GPU adapters. The RTX 4090 record of 2026-10-10 is within them; a CI step checks that
  record and an over-budget control, which must fail.
- **M1 evidence from the RTX 4090** committed: `evidence/m1-post-passes-rtx4090.json` (every post
  pass within one code of its CPU reference) and the board-reported energy of both media scenes
  (`evidence/m1-energy-*-rtx4090.json`, 1.098 J a frame for what-it-draws; not a meter reading).
- **CRT: pixel-locked mask and the `pvm-20-soft` preset** (shader lab 4, #122). The stripe or slot
  mask can lock to the output pixel grid (`mask.lock: "pixel"`, `mask.pitchPx`), on the CPU
  reference and in WGSL. Every CRT parity case is within one code on SwiftShader. Merged just
  before the 0.8.0 tag was cut and not in 0.8.0.
- **Dither temporal stability, measured** (shader lab 4, #126). `tools/shaders/dither_temporal.mjs`
  measures flicker for every dither mode on four sequences and two palettes, under a protocol
  committed before it ran (`evidence/shaders-dither-temporal-runs.json`).
- **Lab 4 blind panel results** (#128). The pixel template now uses `mix-bayer4`, which flickers less
  on moving content under steady light; under a flickering light the mix modes flicker more.
- **PNG reader and one-frame Motion stills** (#121): `tools/shaders/png.mjs` gains `readPng`, and
  `scripts/motion_still.py` renders one lossless frame on SwiftShader.
- **Canvas 2D drawer for Motion** (#125): `web/motion/canvas2d.mjs` draws a Motion display list
  without WebGPU, approximating blur and depth of field.
- **Explainer guides** (#100): the three guides, their rig, worlds and render tools under
  `templates/guides/` and `tools/guides/`.
- **`mix-bayer8` is not a duplicate of `mix-bayer4`.** Shader lab 4 reported it identical by
  construction. That holds at strength 1 only; below it the modes differ, so `mix-bayer8` keeps its
  behaviour and is not made an alias. `docs/shaders/README.md` said "the same indices" without the
  condition and is corrected; `web/shaders/dither-mix.test.mjs` checks both cases on four palettes.

## 0.8.0 (2026-10-14)

- **Ray reconstruction v4 and the conditioning fix** (HW H2, amendment 6). v4 demodulates the path tracer's diffuse
  and specular radiance by their own albedos, filters them separately and adds the primary emission back. Its history
  validity is continuous: v3's threshold tests flipped under float rounding, the recurrence carried the flips, and
  float32 against float64 differed by up to 5.3. v4's gap is at most 7.0e-3 (bound 1e-2), and our Vulkan kernels
  match PyTorch within 7.0e-3 on all three test sequences on SwiftShader. Verdict: FAIL. Tone-mapped PSNR is 5.65 dB
  better than SVGF, but FLIP is worse, and temporal instability and ghosting are worse than SVGF plus TAA, mostly on
  raw_hall. `rr-render --input-only`; a per-kernel timing breakdown for the 2 ms budget. The RTX checks are queued.
Upgrading from 0.7.0: see "Migration from 0.7.0" at the end of this section. Two changes can
break code written against 0.7.0: API snapshot 3 (two `SsrParams` defaults) and the removal of the
0.5 include paths.

- **The 0.5 include paths are removed.** The 28 forwarders `raw/<name>.hpp` (`raw/render.hpp`,
  `raw/vec.hpp` and the rest) are gone; include `raw/<layer>/<name>.hpp`. ADR 0004 scheduled the
  removal for 0.7.0, and 0.7.0 shipped them by mistake, so they lived one release longer than
  promised. `scripts/check_layers.py` now refuses a header placed directly under `raw/`, with a
  selftest case that the old checker accepts.
- **Correctly rounded division in the rasterizer** (RT R1, method note 3 in
  `evidence/rt-r1-bounds.json`). `div_rn` computes binary32 division rounded to nearest even in
  u32 arithmetic and replaces every f32 `/` in `swr_setup`, `swr_resolve` and `swr_gbuffer`,
  because D3D12 permits approximate division. `tests/test_swr_div.cpp` checks the C++ mirror
  against native division on 67,108,864 random pairs and 2,704 edge pairs with 0 differing; the
  control (sticky bit dropped) differs on 51,995. WARP and SwiftShader output is unchanged. The
  RTX 4090 run that tests the inferred cause has not run yet, so R1 on the RTX stays an open
  failure. `SwrParams` gains `div`. The WGSL-to-HLSL translator maps scalar `bitcast` to
  `asuint`, `asint` and `asfloat`.
- **Ray reconstruction v3 measured on the RTX 4090** (HW H2, evidence only). Parity on raw_hero
  misses its 1e-2 bound (cooperative matrix 0.842, scalar 0.404), and the 1080p cooperative-matrix
  median is 9.07 ms against a 2.0 ms budget. Both are recorded failures.
- **Mandelbulb march passes M2** (RT R3). The ray marcher's surface refinement keeps its bracket in double-float
  (TwoSum, additions only), while the march, the estimator and every position stay float32. The bulb now passes M2's
  unchanged bounds on WARP (t within 99.873%, normals 99.535%) and SwiftShader, and the gate covers it. Method note 7
  in `evidence/rt-r3-bounds.json`.
- **Occlusion-aware screen-space reflections** (M3). SSR marches one step a pixel to the screen edge and uses a back
  layer (a second, peeled raster pass), so a ray that enters a solid through a face the camera cannot see ends as a
  miss instead of reflecting the wrong surface. SSR on raw-hall nave passes its unchanged bounds (90.53% correct,
  9.55% false hits; was 68.3% and 11.8%), with every other scene at or above its old rate and GPU parity on WARP and
  SwiftShader. API snapshot 3: two `SsrParams` defaults changed (`api/snapshot-3/MIGRATION.md`). Method note 14 in
  `evidence/m3-post-bounds.json`.
- **Near-plane clipping in the CPU rasterizer** (M3). A triangle with a vertex behind the camera used to be dropped,
  so close views lost geometry: the `close-512` identity view had no ground, and the raw-hall gallery view showed the
  gallery slab's underside through its missing top. Such triangles are now clipped at the near plane; every other
  triangle is drawn exactly as before. GTAO and SSR on raw-hall gallery now pass their unchanged bounds (GTAO mean
  0.0201 and p95 0.1334; SSR 92.0% correct, 5.5% false hits). The identity golden changes only in `close-512`; GCC and
  MSVC agree on the new bytes. Method note 13 in `evidence/m3-post-bounds.json`.
- **Path tracer split radiance, interface v2** (RT). `PathTraceOutput` gains `diffuse`, `specular` and `emission`
  radiance, `diffuseAlbedo` and `specularAlbedo`, and the variance of each component's luminance, on the CPU reference
  and the compute path tracer. Their sum equals `radiance`. The split happens at the first hit, by the BSDF's Lambert
  and GGX terms, with no new random numbers, so every v1 output keeps its bytes (checked against the v1 binary on the
  CPU and on WARP). `rt-pathtrace` writes the new buffers as .pfm. Checks P5a to P5e (`evidence/rt-r2-bounds.json`,
  addendum 5) pass on the CPU, WARP and SwiftShader, and every control fails. The specular albedo is a Schlick estimate
  for demodulation, not the lobe's directional albedo. In browsers the larger per-pixel buffers lower the largest frame
  WebGPU's default 128 MiB binding allows to about 1280 x 720.
- **Inline ray query, criterion H1.1b** (HW). A dated criterion beside H1.1. It uses a hall scene without coincident faces
  and t and uv tolerances divided by the cosine of the ray to the triangle (floored at 1e-3). On the RTX 4090 it passes:
  0 unexplained disagreements and the worst agreeing hit at 0.44 of its tolerance. The offset, rotation and new coplanar
  controls all fail as required. On WARP the only miss left is WARP's TMin defect. H1.1's recorded failures stand.
  `raw_native_cli hw-rayquery --criterion b`.
- **Hardware timings under the three-run protocol** (HW H1.2, H1.3, H1.4, H1.7). Three independent runs on the RTX 4090,
  with GPU snapshots, reported as ranges in `evidence/hw-h1-timing-20261012.json`:
  - cooperative-matrix GEMM: 6.35x to 7.12x the shared-memory fallback (45.0 to 47.8 TFLOPS);
  - mesh shaders: faster only on the close views (1.23x to 1.99x);
  - wave scan: 1.007x to 1.011x;
  - fp16 bilateral filter: 0.976x;
  - async compute: 0.987x to 0.991x, no gain.
- **Ray reconstruction groundwork** (HW H2). Two new commands:
  - `raw_native_cli rr-render` renders dataset frames from the path tracer along camera paths on owned scenes. It writes
    1 spp inputs with G-buffer and motion, and references at 4,096 or 8,192 spp.
  - `raw_native_cli rr-taa` runs M3's TAA resolve over a frame sequence.

  The first trained models do not beat our SVGF baseline. v1 saturated early. v2 trails SVGF by 0.18 dB tone-mapped PSNR
  and by 0.09 FLIP, and is less stable over time than SVGF plus TAA. Full record in `evidence/rr-runs.json`.
- **Ray reconstruction v3** (HW H2, amendment 5). v3 demodulates by an estimate of total surface reflectance instead of
  albedo, and it is chosen by a selection rule whose learning check the saturated v1 fails. On the held-out scenes it beats
  SVGF by 5.95 dB tone-mapped PSNR [5.51, 6.38] and ghosts less than SVGF plus TAA. It still loses on FLIP (+0.031) and on
  temporal stability (+0.0056), so the overall verdict is a fail. Our FLIP now matches the reference implementation's
  test pair (3.5e-7 off the expected mean).
- **Ray reconstruction inference on our own Vulkan kernels** (`raw_native_vk_probe --rrnet`, optional Vulkan build). The
  kernels cover a scalar path and an implicit-GEMM cooperative-matrix path, with 20 dispatches per frame in one
  submission. On SwiftShader the scalar path misses the 1e-2 parity bound. PyTorch's own float32 and float64 passes
  differ by the same amount, so the miss reflects the model's sensitivity, not the kernels. The tensor-core path and the
  2 ms budget were not run: the RTX was held by higher-priority work.

- **Migration from 0.7.0.**
  - Replace each `#include "raw/<name>.hpp"` with its layered path. The mapping is the one the
    forwarders held, for example `raw/render.hpp` to `raw/renderer/render.hpp` and `raw/vec.hpp`
    to `raw/math/vec.hpp`; `git show v0.7.0:raw/<name>.hpp` prints each one.
  - API snapshot 3: two `SsrParams` defaults changed. `api/snapshot-3/MIGRATION.md` lists them
    and how to keep the old behaviour.
  - Additions only, nothing to change: `PathTraceOutput` split radiance (interface v2),
    `SwrParams::div`, `raw_native_cli rr-render`, `rr-taa` and `hw-rayquery --criterion b`.
  - Certificates and receipts keep their schemas; the `renderer` string reads `raw-native 0.8.0`.

## 0.7.0 (2026-10-12)

- **The viewer ships as a release asset.** `raw-native-0.7.0-viewer.zip` holds `web/viewer/` with
  every file it loads (the WebGPU build, its loader and their hashes) in the repository's layout,
  so the page runs unchanged from any static host. Its own `SHA256SUMS` lists each file inside it.
- **Clear coat ceiling passes** (roadmap M3, criterion 6). The anisotropic split-albedo table is
  denser (16 azimuth nodes, 48 roughness rows, 16,384 samples per entry) and is now generated once
  by `tools/pbr_tables/aniso_gen.cpp`, committed with its SHA-256 and embedded at build time, so no
  build or `Tables()` call recomputes it. A white anisotropic metal under a clear coat now stays at
  most 5.9e-4 over a white furnace (ceiling 1e-3; it was 2.65e-3), and the uncoated anisotropic
  furnace improved from 2.7e-3 to 9.3e-4. `test_pbr_aniso_table` checks the hash and regenerates
  64 seeded entries in CI, with two controls. The clear coat check now integrates anisotropic
  points with a finer rule, because the old one was shown to be 1.8e-3 off on the most
  anisotropic lobe; the old rule's figure on the new table (1.23e-3) is kept in the evidence.
  GPU material parity on WARP: 0.0535 of its bound.
  - API snapshot 2: three grid constants of `raw::pbr::Tables` changed value. See
    `api/snapshot-2/MIGRATION.md`.
  - The plugin C ABI part of criterion 5 is deferred by dated note until a real plugin consumer
    exists.
- **Verified viewer** (roadmap M3, criterion 4; decision A4). `web/viewer/` opens a glTF or GLB
  file, renders it on the GPU through WebGPU and on the CPU reference, and shows the two frames,
  the error map between them and the GPU certificate with its verdict. The owned assets load with
  one click. CI drives it headless on SwiftShader: raw-hero and raw-hall render `verified`, and a
  broken GLB must render nothing.
  - Fixed, found by that control: a model that failed to load on the `--gpu` or `--bench` paths
    escaped `main` as an exception; it now exits 2 with `cannot load --model`, as the CPU path did.
- **API snapshot 1** (roadmap M3, criterion 5; ADR 0015). The renderer and scene APIs are a
  numbered snapshot: `kApiSnapshot` in `raw/core/api_snapshot.hpp`, a declaration dump of 25
  headers in `api/snapshot-1/`, a CI check that fails when a declaration is removed or changed,
  and a compatibility suite written the way a user writes code against the snapshot. Additions
  never break it; a break moves to snapshot 2 with a migration note. The plugin C ABI is not
  built, so that part of the criterion stays open.
- **Clear coat energy compensation** (roadmap M3, criterion 6, second stage). Light the coat
  reflects back down to the base is now returned by a reciprocal lobe, on the CPU reference and
  in `pbr.wgsl`. A white metal under a clear coat stays within 1.05% of a white furnace; it lost
  up to 23% before. The split-albedo tables moved to a sqrt(mu) view axis, and the anisotropic
  tables to sqrt(mu) and anisotropy-squared axes; the anisotropic furnace improved from 0.34% to
  0.27%. GPU material parity on WARP fell from 0.62 to 0.054 of its bound.
  - Open failure: the check's ceiling, albedo at most 1 + 1e-3, fails on anisotropic bases by up
    to 2.65e-3. The same base without a coat reaches 1.0027, so the error is the anisotropic
    table's, passed through the coat.
- **TAA passes its RMSE and GPU parity bounds** (roadmap M3, criterion 2). Pixels without
  geometry carried no motion, so silhouettes lagged a frame behind in the history under a camera
  pan; each pixel now takes the motion of the nearest surface in its 3 x 3 neighbourhood, as
  Karis described in 2014. On the 0.3-pixel pan, TAA's RMSE falls from 0.735 to 0.442 of the
  single-sample RMSE (bound 0.5); ghosting stays at 0.0056 (bound 0.03). On the GPU, float32
  rounding noise in the chroma of grey pixels crossed the colour clip's 1e-7 floor; the floor is
  now 1e-4 on both sides, and the GPU and CPU resolves agree on every pixel of every frame on
  WARP and SwiftShader.
  - Open failures, unchanged: SSR on raw-hall (68% and 65% correct, bound 90%) and GTAO in its
    gallery. Probes show a single depth layer cannot tell a ray passing behind a column from one
    striking it.
- **Shadow maps match the CPU rasterizer on D3D12** (roadmap M3, criterion 8). Each cascade's depth
  range now encloses the whole scene, so no depth plane cuts geometry; before, the far plane crossed
  the ground and texels on that line were a float coin toss on every backend, the RTX 4090 included.
  raw-hall's arcade and end walls had coplanar tops that z-fought; the walls now rise to the roof
  slab and stand on footings, and an overlap scan keeps coplanar faces out of the owned assets.
  The map identity passes on WARP and the RTX 4090 and is gated in CI on WARP. One flute sliver
  passes by a thin margin (9.9e-6 against 1e-5). Open failure: SwiftShader's 4-bit edges.
  - Open failure, unchanged: the G-buffer normal identity on raw-hall fails on column-flute slivers
    (RTX 4090 one pixel, ratio 1.26; WARP four pixels, 14.4). The CPU matches the snapped float64
    value; the GPUs differ by less than one snap step's worth on triangles whose normal turns tens
    of degrees across them.
- **Material model, second stage** (the author's decision, 2026-10-10: "keep engine versions, and
  improve them.").
  - Iridescence on dielectric bases uses exact Fresnel amplitudes for both polarizations and a
    colour response tabulated from its reference's own colour matching fit. Against an exact
    polarized thin-film reference, its worst error falls from 0.26 to 0.019.
  - Volume attenuation follows each refracted direction's own path.
  - The specification's forms stay behind `Material::specExact` as a comparison control.
  - Open failure: a white metal under a clear coat loses up to 23% of its energy (bound 2%).
- **HDR signal check and a float32-stable ACES 2.0 hue** (roadmap M1, criterion 3 as amended on
  2026-10-10). The HDR signal is checked in software against the published PQ, sRGB and tone-map
  formulas, and the GPU encode against the CPU. That checks the signal, not the light a panel emits.
  - The first SwiftShader run failed the extended sRGB half on 185 saturated blues. A float32 run
    of the CPU reference at the precision WGSL allows reproduced it: the hue's atan2 may be off by
    4096 ulp, and the hue tables index by whole degree.
  - The hue now comes from an arithmetic atan2 (within 3 ulp), and cos and sin of the hue from
    the opponent coordinates directly, on the CPU and in WGSL. With the bound unchanged, SwiftShader
    passes at 0.064 of it. The CPU reference moved by at most 4.1e-5.
- **Hardware capability probe** (HW workstream H1.0). `raw_native_cli hw-probe` reports what the
  D3D12 adapter offers beyond the RHI: ray tracing tier, inline ray query, opacity micromaps, mesh
  shaders, wave and 16-bit math, bindless, enhanced barriers, work graphs, async compute and
  timestamps. `--checks` holds the answer to account.
  - Wave, 16-bit and ray query pipelines are created exactly when the probe says so. The wave scan
    and the 16-bit arithmetic are bit-equal to the CPU.
  - GPU timestamps double with doubled work (ratio 1.97 on WARP). A fixed-work control fails, as
    it must.
  - `RAW_NATIVE_HW_DISABLE` forces any feature off, and the fallbacks run and match.
  - Shader execution reordering and cooperative matrices have no query in the Windows SDK
    10.0.26100 headers, so they go through Vulkan.
- **Mesh and amplification shaders** (HW H1.3). `raw_native_cli hw-mesh` draws a visibility buffer
  with meshlet frustum and normal-cone culling. It matches vertex pulling bit for bit in triangle index
  and depth on six views. **Open miss:** the flipped-cone control changes nothing in one view (hall,
  close), where every visible meshlet has no cone. On the RTX 4090 the mesh path is faster in all three
  runs where culling drops about half the meshlets (1.3 to 2.5x). On the other views even the sign
  changes between runs, so no claim is made there.
- **Wave intrinsics and native fp16** (HW H1.2). `raw_native_cli hw-wave16` runs a full prefix sum of
  16.7 million values with wave intrinsics and with group-shared memory, plus a 9 x 9 HDR bilateral
  filter in fp16 and in fp32. The scans are bit-equal to the CPU, and fp16 stays within 2^-8 of fp32.
  A dropped lane and a subnormal-range input both fail, as they must. On the RTX 4090, over two quiet
  runs, the wave scan is about 1% faster than the group-shared one, and the fp16 filter is about 2% slower
  than fp32. A first window of RTX timings was disturbed and is withdrawn (evidence/hw-h1-2-runs.json).
- **Async compute** (HW H1.4). `raw_native_cli hw-async` runs a producer on a compute queue beside the
  direct queue, joined by a fence. The results are bit-equal to the serial order and the CPU. A
  consumer that waits on the wrong fence value reads the zero sentinel, so the check can fail. On the
  RTX 4090 async is about 1% slower than serial for two identical kernels: no overlap gain.
- **Cooperative matrix GEMM** (HW H1.7, Vulkan). `raw_native_vk_probe --gemm` multiplies fp16 matrices
  on tensor cores through `VK_KHR_cooperative_matrix` and with a shared-memory fallback. Every checked
  element must sit within K 2^-23 times the sum of |a||b| of a float64 reference. Wrong-layout and
  skipped-K controls must fail. On SwiftShader the fallback passes at 1024 and 2048, and its control
  fails. On the RTX 4090 the tensor-core path is within the bound at every size and reaches 45 to 47
  TFLOPS, 6.4 to 7.1x our fallback, in two separate windows.
- **Inline ray query against the CPU BVH** (HW H1.1). `raw_native_cli hw-rayquery` builds DXR
  acceleration structures on the hw compute queue and traces 1,020,000 rays over three procedural
  scenes, compared hit by hit with M3's BVH. Offset and rotation controls fail on every scene, as
  they must. **Open failure on WARP:** 20 hits below TMin (a WARP defect, reproduced on a single
  triangle), coplanar ties in the hall scene above the explained-fraction bound, and two grazing hits
  outside tolerance. **On the RTX 4090:** no unexplained disagreement on any scene, so the TMin hits were
  WARP's. The coplanar-tie and grazing-tolerance misses remain open. No speed claim yet (no compute baseline).
- **Vulkan hardware probe** (HW H1.0, optional `RAW_NATIVE_VULKAN=ON`). `raw_native_vk_probe` reports
  the same features through Vulkan, adding cooperative matrix configurations and invocation reordering.
  It refuses any non-CPU device unless `RAW_NATIVE_VK_HW=1` is set. On SwiftShader with the validation
  layer, the subgroup scan is bit-equal to the CPU and timestamps scale 2.01 with doubled work. A
  deliberate push-constant misuse is caught, so a clean validation run means something.

- **Owned M3 assets** (the author's decision, 2026-10-10: "No test models, only full owned
  assets."). All three are generated by code in this repository, byte-identical on every compiler.
  - `raw-hero` is a helmet-class asset carrying all nine material extensions.
  - `raw-hall` is an interior at Sponza scale: 586,424 triangles, 37 x 14 x 17 m.
  - `raw_native_cli export-assets` writes them as glTF; `assets/PROVENANCE.md` records where they
    come from.
  - The M3 shadow and RHI v3 checks now run on these assets, and no M3 check reads a third-party
    model. Open failures recorded: raw-hall's shadow maps on WARP and SwiftShader, and four of
    raw-hall's G-buffer pixels on WARP.
- **GTAO, screen-space reflections and TAA** (roadmap M3), on the CPU, D3D12 and WebGPU.
  - GTAO matches ray casts on the near test scene and the raw-hall nave. Mirror SSR matches them on
    both test-scene views. Open failures: in raw-hall's gallery GTAO is 0.073 off on average
    (bound 0.05), and SSR finds 65 to 68% of the hall's reflections (bound 90%).
  - TAA keeps ghosting behind a moving object to 0.014 (bound 0.03). Open failure: under a slow
    pan its error is 0.735 of a single sample's (bound 0.5).
  - The GPU passes match the CPU on WARP and SwiftShader for GTAO and SSR. Open failure: TAA's
    GPU history drifts from the CPU's over 32 frames, from one pixel a step.
  - `raw_native_cli post-parity` runs the GPU checks.
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
- **RT open failures narrowed** (method notes in `evidence/rt-r2-bounds.json` addenda 3 and 4,
  `evidence/rt-r3-bounds.json` notes 5 and 6). R1 gains `swr::Options::snapCorrect` (off by default):
  coverage from snapped vertices, depth and attributes from the unsnapped planes. With it, P4 goes
  from 133 and 137 albedo pixels to 0 and 1. The height-fog march uses composite Simpson, so M4
  passes at 2.9e-5. A trig-free power-8 Mandelbulb estimator on the GPU lifts the bulb to 99.475%
  (t) and 98.2% (normals); it stays open, limited by the float32 march. M3's raster identity
  failure is diagnosed in `evidence/rt-raster-identity-diagnosis.json`, with no raster change.
- **Ray marching: an SDF scene graph on the CPU and the GPU** (RT stage R3, `evidence/rt-r3-bounds.json`).
  - A postfix SDF program provides primitives, smooth union, subtraction, intersection, frames and
    repetition. It also includes the Mandelbulb and Mandelbox estimators the site's fractal work uses.
  - Sphere tracing refines each hit with a sign bracket and bisection. The stop rule alone was off
    by up to 0.5 / cos(theta) at grazing angles. Hits now land within 2e-4 (1 + t) of the analytic
    answer for a sphere, a box and a torus.
  - On the GPU, float32 matches float64 on the garden, the arcade and the Mandelbox scene. Soft
    shadows, AO, god rays and fog closed forms agree too, and raster-plus-SDF compositing through
    shared depth disagrees on no pixel.
  - Open failures: the Mandelbulb in float32 (the same estimator in float32 on the CPU departs
    further), and a 256-sample fog march on optically deep rays (midpoint-rule error, confirmed in
    float64).
  - `raw_native_cli sdf-render` writes the scenes with albedo, normal, depth and material AOVs;
    `sdf-parity` runs the checks.
- **Ray tracing: a BVH built by compute, and a path tracer** (RT stage R2, `evidence/rt-r2-bounds.json`).
  - PLOC (Meister and Bittner 2018) builds the BVH on the GPU without atomics. The GPU tree equals
    the CPU tree node for node, up to 100,760 triangles, on D3D12 WARP and on WebGPU SwiftShader.
  - Traversal gives the brute-force answer on every one of 100,000 rays a scene. The SAH cost is
    within 1.22 of the M3 binned build.
  - A compute path tracer: next-event estimation with MIS, Lambert plus GGX (VNDF sampling) and a
    deterministic seed. It writes radiance and the AOVs albedo, normal, depth, motion, variance and
    triangle. Interface v1 is documented in `raw/renderer/rt_pathtrace.hpp`.
  - Furnace boxes land within 1.2 standard errors of E / (1 - a). Against the CPU reference at 256
    spp, mean |z| is 0.82 (0.80 expected), and the same seed gives the same bytes.
  - Open failure: P4's albedo and depth against the R1 rasterizer. A float64 answer sides with the
    path tracer on 264 of 270 failing pixels, so the cause is R1's vertex snapping, which the
    committed exemption did not allow for.
  - `raw_native_cli rt-pathtrace` writes frames and AOVs; `rt-bvh-parity` and `rt-pt-parity` run
    the checks.
- **raw-native's own triangle rasterizer** (RT stage R1, `evidence/rt-r1-bounds.json`). One
  specification in two forms: a deterministic CPU reference and a GPU compute rasterizer with
  binned tiles and a visibility buffer, without fixed-function raster or atomics.
  - 8 sub-pixel bits, exact 64-bit edge functions (pairs of 32-bit words on the GPU), the
    top-left fill rule, near, far and guard-band clipping, and perspective-correct attributes.
  - Retro modes are options on both forms: affine texture mapping, vertex snapping to whole
    pixels or coarser, and 12-bit depth with a 4 x 4 ordered dither.
  - CPU checks: shared-edge meshes are covered exactly once, and visibility matches BVH ray
    casts on three owned scenes. UVs are within 1.6e-4 of an analytic float64 answer.
  - From the same setup, the GPU matches the CPU bit for bit on every pixel, scene and mode, on
    D3D12 WARP and on WebGPU SwiftShader. With its own setup, every pixel still matches.
  - The scenes are procedural and built in the repo: a BRender-era room, an isometric street and
    a clipping stress scene. `raw_native_cli swr-render` draws them; `swr-parity` runs the GPU checks.
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
