<!-- writing-profile: research -->
# Style passes in the C++ core: port status

Dated 2026-10-15, round 2 added 2026-10-16. The author's decisions of 2026-10-15: "No, renderer
first" and "One C++ core". Design: [ADR 0019](../docs/architecture/adr/0019-style-passes-in-the-core.md).
Bounds, written before the first run: `evidence/cpp-port-style-bounds.json` (round 1, with
Addenda A and B) and `evidence/cpp-port-style-bounds-2.json` (round 2, with Addendum A) and
`evidence/cpp-port-style-bounds-3.json` (hysteresis and palettelight, with Addenda A and B) and
`evidence/cpp-port-style-bounds-4.json` (palette dither and development adjacency) and
`evidence/cpp-port-style-bounds-5.json` (crt-classic, VHS and fibre), `-bounds-6.json` (film) and `-bounds-7.json`
(lens suite, with Addendum A) `-bounds-8.json` (sag and caricature) `-bounds-9.json`
(the one-ray visibility estimator) `-bounds-10.json` (pixel art, with Addendum A), `-bounds-11.json` (the painterly shader, with Addendum A) and `-bounds-12.json` (the physical CRT, with Addenda A and B). Runs:
`evidence/cpp-port-style-runs.json`, `-runs-2.json`, `-runs-3.json`, `-runs-4.json`, `-runs-5.json`, `-runs-6.json`, `-runs-7.json`, `-runs-8.json`, `-runs-9.json`, `-runs-10.json`, `-runs-11.json` and `-runs-12.json`; the records are
`evidence/cpp-port-style-d3d12-warp.json` and `evidence/cpp-port-style-webgpu-swiftshader.json` (CI).

## How a pass is ported

1. Rewrite its WGSL into the translator subset as a `//@pass style_<name>` section of
   `src/renderer/gpu/shaders/style.wgsl`, with a uniform struct for its parameters. Run
   `python scripts/wgsl_to_hlsl.py`; never edit the HLSL.
2. Write the float64 C++ reference in `raw/renderer/` and `src/renderer/`, in the operation order
   of the JavaScript reference.
3. Add the pass's cases to `tools/port/export_style_fixtures.mjs`, built with the web modules as
   the web parity page builds them.
4. Commit bounds, then run `raw_native_cli style-parity` on the CPU, on WARP and in the WebGPU
   build. Every link carries a control that must fail.

Run it locally:

```
node tools/port/gen_pigments.mjs --check
node tools/port/export_style_fixtures.mjs style-fixtures
# one group while developing (not the check of record): --only poststack | image | quantise | dither | adjacency | signal | film | lens | workgroup | rtvis | pixel | paint | crt
build/Release/raw_native_cli.exe style-parity --fixtures style-fixtures --cpu-only
RAW_NATIVE_D3D12_WARP=1 build/Release/raw_native_cli.exe style-parity --fixtures style-fixtures
```

## Results so far

| Pass | CPU link (C++ against JavaScript, float64) | D3D12 on WARP (GPU against C++) | WebGPU on SwiftShader |
|---|---|---|---|
| Dither lighting, 7 cases (2 with the haze input of PR #146) | tone max 2.2e-16; 0 of 145,152 levels differ; plans equal | 0 of 145,152 levels differ; 8-bit max 0 | 0 of 145,152 levels differ; 8-bit max 0 |
| Paint-over, 4 cases | colour max 1.0e-15; 0 coverage flips | 0 coverage flips; mean at most 8.0e-5 codes, max 1 code | identical to WARP |
| Look keys, 6 series | max 0 over 19,607 samples; keys exact; refusals match | no GPU link (CPU terms) | no GPU link |
| Post-stack driver: 12 plans, 3 chains x 3 frames | plans equal, refusals equal; chains 0 ulp, 0 values differ | max relative 1.0e-6 | CI |
| Purity, 4 cases | value max 5.6e-16; 0 codes differ | max 1 code, mean at most 1.3e-5 | CI |
| Lightpaint, 4 cases | value max 2.2e-16; 0 codes differ | max 1 code, mean at most 1.0e-5 | CI |
| Glaze, 4 cases | value max 7.7e-15; 0 codes differ | max 1 code, mean at most 2.6e-5 | CI |
| Hatch, 3 cases | value max 2.1e-14; 0 codes, hits or fallbacks differ | max 2 codes, mean at most 8.8e-4 | CI |
| Hysteresis, 3 sequences (32 frames) | 0 of 2,048,000 decisions and 0 codes differ | 0 decisions differ; max 1 code, mean at most 2.4e-5 | CI |
| Palette-locked lighting, 5 cases | 0 indices differ; the C++-built palette equals the JavaScript one | 0 indices differ; 0 codes | CI |
| Palette dither, 114 cases (6 palettes, 9 modes, 2 plates) | 0 indices differ, 0 ties | 0 indices differ, mix modes included (double-single arithmetic) | CI |
| Development adjacency, 4 cases | value max 5.6e-17; 0 codes differ | max 1 code, mean at most 5.2e-6 | CI |
| crt-classic, 7 cases (960 x 600) | 0 bytes differ; transfer, beam and mask tables equal | max 1 code; 4 cases bit-identical | CI |
| VHS, 3 cases | signal max 0 (every f32 value equal); 0 display codes differ | max 1 code, mean at most 4.0e-5 | CI |
| Fibre-network watercolour, 3 cases | value max 2.8e-16; 0 codes differ | max 1 code, mean at most 7.9e-5 | CI |
| Spectral film, 6 cases (480 x 270) | image max 0 (every f32 value equal); stock tables, halo kernel, grain tables and Poisson thresholds equal | max 1 code, mean at most 5.8e-4 | CI |
| Lens suite, 4 cases (192 x 108) | value max 5.7e-16; plan, starburst table and sources equal | max 1 code; one case bit-identical | CI |
| Scan-causal EHT sag, 4 cases | signal max 0 (every f32 value equal); scan equal | max 1 code; one case bit-identical | CI |
| Salience caricature, 3 cases | value max 0 (every f32 value equal); salience, gradient and lambda equal | max 1 code; one case bit-identical | CI |
| One-ray visibility (RT dither study), 12 cases | visibility equal on every pixel of the 256 x 256 yard, 0 ties; tone max 0; net points equal | no GPU link (CPU code) | no GPU link |
| Perspective-stable pixel art, 5 cases (240 x 150) | 0 probe texels, 0 visibility pixels and 0 codes differ; blend equal | 4 cases bit-identical; one grazing ray in turned-and-close (over2 5.3e-4) | CI |
| Painterly (Kubelka-Munk), 15 cases: 6 stills, 3 camera sequences, 6 post frames (960 x 600) | image max 0 (every f32 value equal); 0 codes differ; tensor weights and canvas layers equal | max 1 code, mean at most 2.2e-3 (after the glaze fix below) | CI |
| Physical CRT, 11 cases (640 x 480; RGB, composite with comb and notch, PAL, persistence, interlace, convergence, pixel-locked masks) | signal within 1e-6; image max 6.0e-8; 0 codes differ; plan, taps (Addendum B) and halo kernel equal | max 1 code, mean at most 8.5e-4 | CI |

Bounds: tone and colour 1e-9; GPU decision flips 5e-4 of pixels, mean 0.12 (dither) and 0.05
(paint-over) codes. Round 2: values 1e-9 and 0 unexcused code flips on the CPU link; the web bounds
of each pass on the GPU link (purity, lightpaint, glaze: max 2, p99.9 1, mean 0.05 codes; hatch:
0.5% of values over 2 codes, mean 0.5); the driver's chains within one f32 ulp on the CPU and 1e-5
relative on the GPU. Each source edit (a constant in the C++ reference or in the WGSL) failed the
link that reads it. One edit taught something: scaling the reference's paint-over colour by
255/256 passes the GPU link, because 8-bit rounding hides it, and fails the CPU link. The chain
needs both links. Round 2 found the same once more: the lightpaint luminance weight 0.7152 to 0.72
in the WGSL passed WARP (the weight also sets the luminance the result is rescaled to), and the
same edit in the C++ failed the CPU link on every case. Nine of the ten round 2 source edits failed
as predicted. For hysteresis, two controls could not fail on the flicker sequence (a wider margin
and a narrower keep interval change no held decision there); the built-in control became margin 0
before any GPU run (bounds-3 Addendum A), and the source-edit miss is recorded (Addendum B).

crt-classic, VHS and fibre (bounds-5) run as frame graphs of `style.wgsl` passes through a small
helper (`src/renderer/gpu/style_graph.hpp`). crt-classic's reference is the author's integer and
Float32Array tube stage, so its CPU link is exact: every byte of the seven 960 x 600 cases, and the
transfer, beam and mask tables entry by entry. VHS's C++ signal equals the JavaScript signal on
every f32 value. All six source edits of bounds-5 failed as predicted.

The spectral film shader (bounds-6) needs the CIE 1931 tables, generated from
`web/shaders/cie1931.mjs` into `src/renderer/data/cie1931.hpp` (rule 4, CI `--check`); every
other table (stocks, colour timing, the base-glass halo kernel, the Boolean grain's variance and
Poisson tables) is computed by the C++ and compared. Its image equals the JavaScript's on every
f32 value, the Monte Carlo grain included. The lens suite (bounds-7) ports the paraxial ghost
prescription and the iris's FFT starburst. One source edit missed: the halation constant in the
lens gather pass (2 pi to 6.0) stayed inside the web bound at 192 x 108, where halation is half a
pixel wide; Addendum A records it, and a post-hoc edit of the ghost edge ramp failed every case.
Neither film nor lens needed `arrayLength`: counts reach the passes in their uniforms.

The translator now accepts workgroup memory (`var<workgroup> name: array<T, N>;` becomes HLSL
`groupshared`), `workgroupBarrier()` as a statement and the group builtins
(`local_invocation_id`, `workgroup_id`, `local_invocation_index`), each with a selftest; the
generated files of every earlier pass are unchanged. Sag sums each line's current and caricature
finds its global maximum by trees in workgroup memory, dispatched with explicit group counts
(`style_graph::Passes::addGroups`). Both match the JavaScript to the f32 bit on the CPU and are
within 1 code on WARP; all four source edits failed as predicted. The one-ray visibility estimator
of the RT dither study (`raw/renderer/style_rtvis.hpp`) is CPU code with the CPU link only (rule 6,
like look keys); `StyleParity::gpuPass` does not ask it for a GPU link.

Pixel art needed only `atomicMin` from the translator (as a statement, HLSL `InterlockedMin`): the
texel-index pass reads the depth keys through a plain read-only binding, written by the previous
pass, so `atomicLoad` is not needed, and buffer lengths come from the uniform, so `arrayLength` is not
either. Its C++ reference equals the JavaScript on every texel, pixel and code. One WGSL source edit
(the crease threshold) stayed inside the loose web bound and is recorded as a miss (Addendum A).

The painterly shader runs as ten passes: prep, tensor, two blurs, the anisotropic Kuwahara filter,
stroke, relief and compose, with the advected canvas (decide, advect, distortion) run on the GPU
each frame of a camera sequence. Its C++ equals the JavaScript on every value. The first WARP run
failed two watercolour post frames by up to 29 codes in one channel. The C++ compose on the GPU's
own intermediate buffers matched the CPU, which put the fault in the GPU compose, and a pixel dump
found it: WARP's `sinh` and `cosh` are off by about 1e-4 relative near 13, and yellow's blue channel
is a near cancellation that the glaze ratio divides by a 1e-4 floor. The WGSL now builds both from
one `exp`; the references are unchanged, and every case is within 1 code. Both source edits
failed as predicted.

The physical CRT runs as seven passes: the three signal passes (encode, separate, demod), the
beam pass with the tube geometry, the mask and the phosphor history, and the halo, its
convolution and compose with the display encode. The parameter block of `layout.mjs` and the
filter taps travel as one storage array. The C++ plan resolves presets, phosphor screens
(spectral, through the CIE tables) and the halo kernel from the glass optics itself, and matches
the JavaScript to float64 rounding. The first CPU run failed one check that was mine: a 1e-12
relative bound on the notch filter's near-zero taps, which are cancellation residues; Addendum B
records it. WARP was within 1 code on the first run, with the shoulder's `tanh` built from one
`exp` in advance. Both source edits failed exactly as predicted.

Round 2 adds the post stack's driver (`raw/renderer/post_stack.hpp`): the registry, the encoding
contract and stage split of `web/motion/post.mjs` and `renderer.mjs`, and the run order of
`post_run.mjs` (ping-pong frames, history frames), run as a host frame graph on the CPU references
or a device frame graph on the `style.wgsl` passes. Only `exposure` and `srgb-decode` run through
it so far; each later port registers its pass. A finding for the owner of `web/motion`: the
contract double-decodes a list that names `srgb-decode` before a linear-input pass, and the C++
plan keeps that because the web plan is its oracle.

What this does not show: no hardware GPU was used (software adapters only), the fixtures are two
scenes, and the web passes still carry their own WGSL text (ADR 0019, rule 7).

## Remaining web-only passes

None. With the physical `crt` (round 3, after `paint`), every pass module under `web/shaders/`
and the look curves have a C++ reference, one WGSL source in `style.wgsl` and the parity runs
above; N1's status is in [RELEASE-PLAN.md](RELEASE-PLAN.md). The no-third-party check reads the
C++ references too (`node tools/ci/no_third_party_shaders.mjs --root src/renderer --cpp --prefix
style_`, and the same for `raw/renderer`).
