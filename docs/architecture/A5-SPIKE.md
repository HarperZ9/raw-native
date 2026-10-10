# A5 spike: grow the WGSL translator, or adopt Slang

ROADMAP M2 criterion 4 asks for this report, with measurements, followed by the author's decision. [ADR 0002](adr/0002-rhi.md) sets out A5. [ADR 0013](adr/0013-media-grade-roadmap.md) limits it to the native translator: WGSL that the web host runs as written does not trigger it.

**State: half measured.** The translator side is measured below. The Slang side is not measured, because measuring it needs a download that waits for the author's approval (see "What the Slang half needs").

## What the translator had to grow for M2

M2 criterion 3 (RHI version 2, raw-native #69) needed raster passes with textures and samplers. The translator grew to cover them:

| | Before #69 (603d321) | After #69 (main) |
|---|---|---|
| `scripts/wgsl_to_hlsl.py` + `wgsl_hlsl_expr.py` | 262 + 191 = 453 lines | 345 + 213 = 558 lines (+105) |
| Passes translated | 16 compute | 16 compute + 1 raster |
| Places that refuse an unsupported construct (`TranslateError`) | not counted | 29 |
| `--check` over every pass (CI's drift check) | | 0.47 s |
| dxc, one raster stage (ps_6_0) | | 0.12 s |

The raster subset covers:

- `@vertex` and `@fragment` entry points;
- the `vertex_index` and `position` builtins;
- `texture_2d<f32>` and `sampler` bindings;
- `textureSampleLevel`.

The self-test refuses:

- a fragment shader that returns a builtin;
- storage buffers in raster passes;
- `textureSample`, which takes derivatives;
- entry points out of order.

The first D3D12 run on WARP and the first WebGPU run on SwiftShader matched the CPU sampler reference within bounds (`evidence/m2-rhi-texture-*.json`). No translation bug reached a run.

## What materials will ask of it next (an estimate, not a measurement)

The PBR base model (M2 gap 5) and M3's lit scenes need constructs the subset refuses today:

| Construct | Needed for | Translator work, estimated |
|---|---|---|
| `mat3x3f`, `mat4x4f`, matrix products | normal mapping, view and projection | types plus `mul()` ordering (WGSL is column-major, HLSL `mul` row-vector by default): moderate, and easy to get silently wrong |
| `textureSample` with implicit derivatives | material textures in fragment shaders | small, but derivative behaviour differs at quad edges, so it needs its own identity test |
| `texture_cube`, `texture_2d_array` | IBL (gap 6) | small per type |
| Override constants or another permutation mechanism | material permutations | large: the translator has no preprocessor, and permutations multiply the DXIL count |
| Multi-line statements | readable PBR code | moderate: the line-based design is the subset's main limit |

The rough sum: the translator could double again, to about 1,100 lines, by the end of M3. The riskiest single item is matrix layout. Its failure mode is a plausible wrong picture, and the identity tests catch that only if a test exercises it.

## What the Slang half needs

- **Download:** `slang-2026.19-windows-x86_64.zip`, 63,221,930 bytes, from the shader-slang/slang GitHub release v2026.19 (published 2026-09-29). Licence: Apache-2.0 with LLVM exception (ADR 0002). It goes outside the repository, at `D:/tools/slang/`, and nothing is vendored until the decision.
- **Measurements to take:**
  1. Port the texture-identity raster pass and two compute passes (`ssao`, `shade`) to Slang, and record the lines.
  2. Compile each to DXIL and to WGSL with `slangc`, and record the times.
  3. Run the existing identity tests on Slang's output: the D3D12 path on WARP and the WebGPU path in the browser. Slang's WGSL is what the web would run, so the web host's "WGSL as written" rule would change.
  4. Record the binary size added to a build and to CI setup time.
- **What a fair comparison must count:**
  - Slang adds a 63 MB toolchain and a dependency of the engine's build.
  - It removes the translator's maintenance, and the class of bug where the translator is silently wrong.
  - It changes the web path from hand-written WGSL to generated WGSL.

## Options for the author

1. **Keep growing the translator through M2, and decide again at material permutations.** The cost so far is +105 lines, every construct is refused unless it is translated, and there are no new dependencies. The risk is matrix layout and permutations.
2. **Adopt Slang now**, after the measurement above. One source language covers DXIL, SPIR-V (Vulkan in M4), Metal and WGSL. The cost is the toolchain, and the web shaders stop being hand-written WGSL.
3. **Hybrid:** the translator stays for compute passes, and Slang handles material permutations only. The cost is two paths to keep.

**Provisional reading (mine, on half the evidence):** option 1 holds through M2. The measured cost is small, and the identity tests catch translation errors. Matrix support and permutations are the signal to measure Slang, before M3's lit scenes. This is not a recommendation to skip the Slang measurement: criterion 4 asks for the author's decision with both halves measured.
