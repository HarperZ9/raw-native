# A5 spike: grow the WGSL translator, or adopt Slang

ROADMAP M2 criterion 4 asks for this report, with measurements, followed by the author's decision. [ADR 0002](adr/0002-rhi.md) sets out A5. [ADR 0013](adr/0013-media-grade-roadmap.md) limits it to the native translator: WGSL that the web host runs as written does not trigger it.

**State: both halves measured; the decision is the author's.** The author approved the Slang download on 2026-10-10. The Slang half below was measured with slangc 2026.19, whose zip matched GitHub's published sha256 (`fc922f21...`), installed outside the repository.

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

## The Slang half, measured (slangc 2026.19, 2026-10-10)

**1. The texture-identity raster pass, ported by hand** (`docs/architecture/a5/texture_identity.slang`, 26 lines; the WGSL section is 25).

| Target | Compile time | Result |
|---|---|---|
| DXIL vs_6_0 / ps_6_0 | 789 ms / 212 ms (2,860 / 4,188 bytes) | **The D3D12 identity check on WARP passes, identical to the translator's DXIL:** nearest 0 codes, linear 1, control fails |
| WGSL | 196 ms | **Renders pixel-identical** to the translator-path WGSL in Chrome on SwiftShader: 0 differing values in all four sampler cases |

Slang's WGSL is generated code. It wraps the uniform in a std140 struct (`@align(16)`), suffixes every identifier (`size_0`) and returns through output structs. It runs, but it is not WGSL anyone would hand-write or read in a frame debugger.

**2. Every existing generated HLSL pass, through slangc unchanged** (Slang accepts HLSL), to DXIL and to WGSL:

- 13 of 15 compute passes compile to both targets, about 0.2 s each.
- 2 fail: `setup` and `threads_advance`, with "ambiguous reference to 'k'". Slang applies the old HLSL rule that a `for` loop's variable belongs to the enclosing block, so two loops in one function that both declare `k` collide. dxc with `-HV 2021` scopes the variable to the loop. The fix is mechanical: unique loop variable names, or a block around each loop, in the translator or the WGSL.

**Not measured:** running the compute passes' Slang output through the full GPU renderer's identity matrix. That needs the shader library to load compiled code from files, which this spike did not build. The raster pass is the only one run end to end.

**Costs, measured:**

- The toolchain is 63,221,930 bytes zipped, a build dependency of the engine. It is Apache-2.0 with LLVM exception, which ADR 0005 permits.
- A pass takes 0.2 to 0.8 s to compile, against dxc's 0.12 s.

## Options for the author

1. **Keep growing the translator through M2, and decide again at material permutations.** The cost so far is +105 lines, every construct is refused unless it is translated, and there are no new dependencies. The risk is matrix layout and permutations.
2. **Adopt Slang now**, after the measurement above. One source language covers DXIL, SPIR-V (Vulkan in M4), Metal and WGSL. The cost is the toolchain, and the web shaders stop being hand-written WGSL.
3. **Hybrid:** the translator stays for compute passes, and Slang handles material permutations only. The cost is two paths to keep.

**My reading, now on both halves:**

- Slang is a working, lower-risk path off the translator. The raster pass is identical on both backends, 13 of 15 existing passes compile untouched, and the other two need a rename.
- What Slang costs is the web's hand-written WGSL: the web would run generated code.
- So option 3, the hybrid, fits the evidence best. The native compute and raster passes move to Slang when matrices and permutations arrive (M3), and the web host keeps hand-written WGSL where it runs WGSL as written today.
- Option 1 remains defensible through M2.

The decision is the author's.
