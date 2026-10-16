# ADR 0019: style passes in the C++ core, from one WGSL source

**Status:** Accepted for the passes it covers (dither lighting, paint-over projection, look
keys); the rule applies to every later port. **Date:** 2026-10-15. **Builds on:** 0002 (A5),
0005, 0006, 0010, 0018 (the author's D1 and D2 decisions are recorded there).

## Context

The author, 2026-10-15, answering the release plan's decisions: "No, renderer first" (D1) and
"One C++ core" (D2): one C++ engine, built natively and for the browser through WebAssembly,
with the shader passes shared.

Until now the style passes (dither lighting, paint-over projection, film, CRT, palette dither
and the rest) lived only in `web/shaders/`. Each has a float64 JavaScript reference and a WGSL
pass embedded in a JavaScript string. That WGSL is outside the subset `scripts/wgsl_to_hlsl.py`
translates: untyped `let`, several statements on one line, a `P: array<f32>` parameter block
with no declared layout. ADR 0010 asked for "a shader written once, in WGSL, in the subset", and
the style passes drifted from that rule.

A port needs, for each pass, a C++ CPU reference, a D3D12 path and a WebGPU path. The risk is
four copies of one effect (JS reference, web WGSL, C++ reference, HLSL) that drift apart one
edit at a time.

## Decision

**1. The shader source is translator-subset WGSL in `src/renderer/gpu/shaders/style.wgsl`.**
Each pass is one `//@pass style_<name>` section. `scripts/wgsl_to_hlsl.py` generates
`hlsl/style_<name>.hlsl` and `style_layout.hpp` (the binding list), and CI's `--check` fails when
the WGSL changed and the HLSL was not regenerated. The WebGPU build runs the same WGSL bytes
(embedded by `cmake/gpu.cmake`). Nobody edits the HLSL.

Why WGSL, and why through the existing route:
- The browser can only run WGSL. With WGSL as the source, the web host runs the source as written
  and only the native targets are generated. Any other source language needs a translator into
  WGSL as well.
- The route exists, costs no dependency (ADR 0005) and is already checked in CI. The two shaders
  ported here fit the subset after a mechanical rewrite (explicit types, one statement a line, a
  uniform struct), which this ADR's first PR shows by translating them.
- ADR 0002's A5 decision keeps the subset until textures and permutations strain it. Neither
  pass needs a construct the subset refuses. A pass that does reopens A5 (grow the translator or
  adopt Slang); it does not get a hand-written HLSL file.

**2. The C++ CPU reference is float64 and hand-written, one per pass, in the renderer layer**
(`raw/renderer/style_ditherlight.hpp`, `style_paintover.hpp`, `look_keys.hpp`). A reference has
to be a separate implementation of the mathematics: if it were generated from the shader, a
shader bug would be in both and every parity check would pass. The cost is a second copy of the
logic, so the next rule exists to catch drift.

**3. The JavaScript float64 reference stays as a test oracle** (the release plan's D2 wording).
A Node script, `tools/port/export_style_fixtures.mjs`, builds the cases of the web parity bounds
files with the web modules exactly as they are (the same fixtures, the same G-buffers, the same
f32 inputs), and writes the inputs and the JavaScript outputs to a binary bundle. The C++ check
reads the bundle, runs the C++ reference, and compares within bounds committed before the first
run. So the four implementations are tied in a chain, each link checked in CI:

| Link | Check | Bound kind |
|---|---|---|
| JS float64 reference against C++ float64 reference | `style-parity` CPU section, on the exported fixtures | float64 rounding: 1e-9 on tones and colours; level and coverage decisions equal except at recorded ties |
| C++ float64 reference against the WGSL pass on D3D12 (WARP) | `style-parity` GPU section on the D3D12 build | f32 against f64: fractions of pixels whose decision flips, as in the web bounds |
| C++ float64 reference against the WGSL pass on WebGPU (SwiftShader) | the same command in the WebAssembly WebGPU build | as above |
| web WGSL against the JS reference | the existing `tests/web/shaders_parity.py` | unchanged |

A change to any one implementation that changes its output breaks at least one link.

**4. Data that must match bit for bit is generated once and committed.** The 64 x 64 blue-noise
ranks come from the JavaScript void-and-cluster generator, written by
`tools/port/gen_bluenoise.mjs` into `src/renderer/data/bluenoise64.hpp`, and CI regenerates and
compares it. Re-running the generator in C++ would depend on each C++ library's `exp` agreeing
with V8's to the last bit, because the generator breaks ties by comparing energies.

**5. One declaration per layout.** A pass's parameters are a WGSL uniform struct; the C++ host
mirrors it with a `static_assert` on its size, as the M3 post passes do. The binding list comes
from the generated layout header.

**6. Terms with no shader port as CPU code with the same oracle link.** Look keys evaluate per
frame on the CPU and reach the GPU only as uniform values, so they get the CPU link and no GPU
link. They sit in the renderer layer until the environment-state stream (ADR 0017) creates the
world layer, and then move there unchanged.

**7. The web modules converge later, not in this change.** `web/shaders/` has its own owner. The
convergence step is for each web pass to import the WGSL generated from `style.wgsl` (a string
module) in place of its embedded copy; until then the third link above keeps the two WGSL texts
honest by output, not by text.

## Consequences

- A port is: rewrite the WGSL into the subset, write the C++ reference, add the pass to the
  fixture exporter, commit bounds, run CPU, WARP and SwiftShader. The web module is untouched.
- The C++ references grow the library (about 300 lines for the first three ports). The export
  bundle is built in CI from the JavaScript fixtures and never committed.
- `style-parity` runs in three places: the native CLI (CPU only), the D3D12 build on WARP, and
  the WebGPU build on SwiftShader. Without an adapter it runs the CPU link and says the GPU link
  was skipped, never passed.
- CI's original-code check covers `src/renderer/gpu/shaders/` as well as `web/shaders/`.

## Alternatives considered

- **The web WGSL as the source, translated as written.** Needs a real WGSL parser in the
  translator (the dialect has untyped declarations and statement sequences on one line), and makes
  the browser layer the core's source of truth, the opposite of D2.
- **HLSL as the source, translated to WGSL.** No route exists, and the browser target would
  become the generated one, where a translation error is hardest to see.
- **Slang as the source now.** A dependency with no pass that needs it yet; A5 assigns that
  choice to the dependency policy when the subset is strained.
- **Generate the C++ reference from the WGSL.** Removes a copy and also removes the independence
  that makes a parity check mean something.

## What would reverse it

- A pass that cannot be written in the subset without contortion: reopen A5.
- The cross-language link costing more upkeep than it catches (for example, bound changes on more
  than one port in five for reasons that are not bugs): reconsider keeping the JavaScript
  references as oracles once every web pass imports the core's WGSL.
