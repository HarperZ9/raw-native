<!-- writing-profile: research -->
# Style passes in the C++ core: port status

Dated 2026-10-15. The author's decisions of that day: "No, renderer first" and "One C++ core".
Design: [ADR 0019](../docs/architecture/adr/0019-style-passes-in-the-core.md). Bounds, written
before the first run: `evidence/cpp-port-style-bounds.json` (with Addendum A). Runs:
`evidence/cpp-port-style-runs.json`; the WARP record is `evidence/cpp-port-style-d3d12-warp.json`.

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
node tools/port/export_style_fixtures.mjs style-fixtures
build/Release/raw_native_cli.exe style-parity --fixtures style-fixtures --cpu-only
RAW_NATIVE_D3D12_WARP=1 build/Release/raw_native_cli.exe style-parity --fixtures style-fixtures
```

## Results so far

| Pass | CPU link (C++ against JavaScript, float64) | D3D12 on WARP (GPU against C++) | WebGPU on SwiftShader |
|---|---|---|---|
| Dither lighting, 7 cases (2 with the haze input of PR #146) | tone max 2.2e-16; 0 of 145,152 levels differ; plans equal | 0 of 145,152 levels differ; 8-bit max 0 | run in CI (WGSL compiles on SwiftShader) |
| Paint-over, 4 cases | colour max 1.0e-15; 0 coverage flips | 0 coverage flips; mean at most 8.0e-5 codes, max 1 code | run in CI |
| Look keys, 6 series | max 0 over 19,607 samples; keys exact; refusals match | no GPU link (CPU terms) | no GPU link |

Bounds: tone and colour 1e-9; GPU decision flips 5e-4 of pixels, mean 0.12 (dither) and 0.05
(paint-over) codes. Each source edit (a constant in the C++ reference or in the WGSL) failed the
link that reads it. One edit taught something: scaling the reference's paint-over colour by
255/256 passes the GPU link, because 8-bit rounding hides it, and fails the CPU link. The chain
needs both links.

What this does not show: no hardware GPU was used (software adapters only), the fixtures are two
scenes, and the web passes still carry their own WGSL text (ADR 0019, rule 7).

## Remaining web-only passes

Estimates are mine, in hours of one agent's work at the pace of this port, moderate to low
confidence (plus or minus half). "Subset work" is translator growth the pass needs before its
WGSL can be generated.

| Pass (`web/shaders/`) | JavaScript lines | Entry points | Subset work | Estimate |
|---|---|---|---|---|
| `purity` | 152 | 1 | none | 1.5 h |
| `lightpaint` | 173 | 1 | none | 2 h |
| `glaze` | 195 | 1 | none | 2 h |
| `hatch` | 201 | 1 | none | 2 h |
| `hysteresis` | 212 | 1 | none; needs a previous-frame buffer in the check | 2.5 h |
| `adjacency` | 199 | 4 | none (`round` of integer parameters becomes u32 uniform fields) | 3 h |
| `crt-classic` | 216 | 5 | none | 3 h |
| `vhs` | 245 | 4 | none | 3 h |
| `fibre` | 230 | 5 | none | 3 h |
| `dither` (palette, ordered, blue noise, mix) | 420 | 1 | none; the blue-noise table is already in the core | 3 h |
| `palettelight` (open PR #149) | not on `main` | 1 | none expected | 3 h after it merges |
| `caricature` | 195 | 5 | workgroup memory and barriers (`var<workgroup>`, `workgroupBarrier`) | 4 h, plus 1 h of translator work shared with `sag` |
| `sag` | 194 | 3 | workgroup memory and barriers | 3 h |
| `film` | 709 | 5 | `arrayLength`; spectral tables | 6 h |
| `pixel` | 621 | 7 | `atomicMin`, `atomicLoad`, `atomicStore`, `arrayLength` | 6 h, plus 1.5 h of translator work |
| `paint` (painterly, Kubelka-Munk) | 1,073 | 11 | atomics, `unpack4x8unorm` | 10 h |
| `crt` (physical) | 1,231 | 8 | none found | 10 h |
| `post.mjs` (the web post stack's ordering and ping-pong) | 219 | none | a C++ post-stack driver over the frame graph | 3 h |

Total about 75 hours. Order by need: the R1 slice needs paint-over and look keys (done), then
`dither`, `hysteresis` and `film` for the templates, with the translator growth done once before
`caricature`, `sag` and `pixel`.
