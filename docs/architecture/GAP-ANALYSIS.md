# Gap analysis: raw-native against mature engines

Written 2026-10-04 against raw-native at the layered-architecture change. It
compares raw-native with bgfx, Filament, wgpu, Bevy, The Forge, Godot 4, and
Unreal and Unity as their public documentation describes them. Every external
claim cites a source read on 2026-10-03, listed under [Sources](#sources).
Claims marked **[memory, unverified]** come from prior knowledge and were not
checked against a source this time.

## Result

raw-native leads on one axis and trails on most others. It is the only one of
these projects that checks its fast paths against a reference renderer on
every run and writes the verdict beside the pixels. On features it is at the
start: compute-only RHI, two backends, one built-in scene, no textures, no
window. The table orders the work; the [roadmap](ROADMAP.md) turns it into
milestones with exit criteria.

Difficulty is relative effort for one maintainer: **S** days, **M** weeks,
**L** one to three months, **XL** longer.

## Subsystems

| # | Subsystem | raw-native has | Missing | Difficulty | Order |
|---|---|---|---|---|---|
| 1 | RHI | Buffers, compute pipelines, uploads, copies, dispatch, barriers; D3D12, WebGPU, null; generational handles; debug-layer gating | Textures, samplers, graphics pipelines, render passes, swapchain, queries, multiple queues, bindless, Vulkan, Metal, a validating layer | XL | 1 |
| 2 | Frame graph | Validation, culling, barrier planning, host and device execution, `describe()` | Textures and subresources, transient aliasing, history resources, async compute, split and enhanced barriers, parallel recording, a visualizer | L | 2 |
| 3 | Memory | CPU arena with a budget certificate; one committed GPU resource per buffer | GPU suballocation, budgets and residency, staging rings, aliasing heaps | M | 4 |
| 4 | Scene | Meshes, one material colour, one directional light, a camera; one built-in scene | Render scene with handles, instances and transforms, frustum culling, GPU BVH, `superstack.scene/1` import | L | 3 |
| 5 | Assets | None (the built-in scene) | glTF 2.0 import, PNG and JPEG, KTX2 and Basis Universal, mesh optimization, cooking, an asset database, hot reload | L | 3 |
| 6 | Materials and lighting | Lambert albedo, one directional light, SSAO and ray-traced AO, an unclamped HDR channel | Metallic-roughness PBR, image-based lighting, punctual lights, clustered lighting, shadows, exposure and tone mapping, anti-aliasing, post | XL | 5 |
| 7 | Shaders | One WGSL source, a checked translator to HLSL, DXC offline, a binding layout generated from the WGSL | Permutations, textures in the subset, SPIR-V and MSL, a pipeline cache, hot reload; possibly Slang (A5) | L | 5 |
| 8 | Jobs and threading | Row-parallel loops with output identical for any thread count | A job system with dependencies, a render or submission thread, parallel recording | M | 6 |
| 9 | Streaming and LOD | None | Async loading, mip streaming, mesh LODs | L | 9 |
| 10 | Platform | A CLI; the wasm loader with hash checks | Windows, input, swapchain present, HDR output, timers, a file abstraction | M | 4 |
| 11 | Audio hook | None in raw-native; superstack's sound contract and the site's sound layer exist | An audio interface writing `superstack.sound/1` receipts | S | 10 |
| 12 | Plugins and scripting | None | Registration tables, the C ABI with versioned tables, an API dump with CI validation | L | 7 |
| 13 | Editor and tools | CLI, `verify`, `recheck.py`, the identity matrix, browser bench pages | A verified viewer (A4), an inspector, a graph visualizer | XL | 8 |
| 14 | Profiling and capture | Wall-clock bench JSON; D3D12 debug names on every buffer and pipeline | GPU timestamps, Tracy zones, PIX and RenderDoc capture hooks | S to M | 4 |
| 15 | Crash reporting | None | Minidumps with build IDs (crashpad or sentry-native, under A2) | M | 9 |
| 16 | Packaging | Release archives with SHA256SUMS, a CMake package, wasm modules checked by hash | Installers, code signing, SBOM, package-manager ports | M | 8 |
| 17 | Accessibility | CLI only; the site engine's reduced-motion and labelling policy | For the viewer: screen-reader names, scalable UI, keyboard reach, colour-blind-safe overlays, reduced motion | M | 8 |
| 18 | Verification | CPU oracle, `raw-cert/2`, `raw-gpu-cert/1`, superstack receipts, an independent Python checker, cross-toolchain identity, the golden identity matrix | Per-device-class GPU goldens on a hardware runner, tolerance families for non-bit-exact features, a glTF conformance corpus with certificates, a graph certificate | M, ongoing | 0 |

## How each strength holds as the engine grows

| Strength | What strains it | How it holds |
|---|---|---|
| CPU reference oracle | Feature count, and frame cost at 1440p | Every feature lands in the reference ([ADR 0006](adr/0006-verification-at-scale.md)); small frames in CI, seeded tiles at full size, threads with identical output |
| Byte-identical determinism | GPUs and drivers differ; FMA contraction on ARM64 | Identity scoped to a device class and recorded; tolerance across classes; `-ffp-contract=off` for the reference before the first ARM64 build |
| Certificates | Many features, many schemas | Schema versions never change shape; each family gets an independent checker; plugins never certify themselves ([ADR 0007](adr/0007-plugin-model.md)) |
| Receipts | A larger pipeline of producers | superstack receipts name every plugin and backend that touched a frame ([ADR 0008](adr/0008-contracts-and-web.md)) |
| Golden images | Output changes on purpose as features land | The golden manifest changes only in a pull request that explains each moved hash |
| Minimal dependencies | Platforms and formats need libraries | Dependencies kept out of the reference and certificate path entirely ([ADR 0005](adr/0005-dependency-policy.md)) |

## Each competitor's lead, recorded as a gap to close

| Project | Where it leads today | The gap for raw-native | Closed at |
|---|---|---|---|
| bgfx | Backend breadth: D3D11 and 12, Metal, GL and GLES, Vulkan, WebGL, WebGPU; offline shader, texture and geometry tools | Three native backends plus WebGPU, and an asset cooker | M3, M4 |
| Filament | A documented physically based model with photometric units and a physical camera; a material compiler with variant filtering; mobile backends | PBR built from the same published model, with a CPU reference of it | M2 |
| wgpu | A validated, portable API with tracked usage and automatic barriers; Naga shader translation | A validating layer over the RHI; broader shader targets | M3 |
| Bevy | An ECS with a parallel scheduler; pipelined rendering with an extract phase; plugin ergonomics | Only if A3 puts a runtime in scope; the `world` layer is ready for it | After A3 |
| The Forge | Console support and performance tuning; async resource loading | Performance work with GPU timestamps; streaming | M4, M5 |
| Godot | A full editor; GDExtension's forward-compatible C ABI; licence-tracked third-party code; Forward+ clustered lighting; GPU-side barrier graph | The viewer (A4), the C ABI, the third-party manifest, clustered lighting | M2 to M5 |
| Unreal | RDG with aliasing, async compute and split barriers; module and plugin system; scale features | Aliasing and async compute in the graph; plugins | M3, M5 |
| Unity | Scriptable pipelines over a render graph that culls and aliases; platform reach; a large asset pipeline | Graph aliasing; the asset pipeline | M1, M3 |

None of these leads is permanent. Each row is a target with a milestone, and a
target met is rechecked against the project's then-current release.

## Sources

Read 2026-10-03. Version labels are what each page showed.

- bgfx 1.146.9292: [build](https://bkaradzic.github.io/bgfx/build.html),
  [overview](https://bkaradzic.github.io/bgfx/overview.html),
  [internals](https://bkaradzic.github.io/bgfx/internals.html),
  [tools](https://bkaradzic.github.io/bgfx/tools.html)
- Filament v1.77.2: [repository](https://github.com/google/filament),
  [FrameGraph notes](https://google.github.io/filament/notes/framegraph.html),
  [materials](https://google.github.io/filament/main/materials.html),
  [physically based rendering](https://google.github.io/filament/main/filament.html),
  [engine architecture (third-party analysis, moderate confidence)](https://deepwiki.com/google/filament/2-engine-architecture)
- wgpu v30.0.1: [repository](https://github.com/gfx-rs/wgpu),
  [wgpu-hal](https://docs.rs/wgpu-hal/latest/wgpu_hal/),
  [wgpu-core](https://docs.rs/wgpu-core/latest/wgpu_core/),
  [naga](https://docs.rs/naga/latest/naga/)
- Bevy 0.19: [bevy_render](https://docs.rs/bevy_render/latest/bevy_render/),
  [render resources](https://docs.rs/bevy_render/latest/bevy_render/render_resource/index.html),
  [bevy_ecs](https://docs.rs/bevy_ecs/latest/bevy_ecs/),
  [0.19 release notes](https://bevy.org/news/bevy-0-19/),
  [0.18 to 0.19 migration](https://bevy.org/learn/migration-guides/0-18-to-0-19/),
  [render stages](https://bevy-cheatbook.github.io/gpu/stages.html)
- The Forge v1.63: [repository](https://github.com/ConfettiFX/The-Forge)
- Godot 4.7 docs: [architecture diagram](https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html),
  [RenderingDevice](https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html),
  [internal rendering architecture](https://docs.godotengine.org/en/stable/engine_details/architecture/internal_rendering_architecture.html),
  [rendering acyclic graph](https://godotengine.org/article/rendering-acyclic-graph/),
  [third-party README](https://github.com/godotengine/godot/blob/master/thirdparty/README.md),
  [GDExtension version compatibility](https://godot-rust.github.io/book/toolchain/godot-version.html),
  [extension API validation](https://github.com/godotengine/godot/pull/76446)
- Unreal Engine 5.8 docs: [modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules),
  [plugins](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine),
  [RHI](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI),
  [render dependency graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine),
  [threaded rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine),
  [global shaders](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine)
- Unity 6 docs: [scriptable render pipeline](https://docs.unity3d.com/Manual/scriptable-render-pipeline-introduction.html),
  [URP render graph](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/render-graph-introduction.html),
  [scripting backends](https://docs.unity3d.com/Manual/scripting-backends.html),
  [CoreCLR update, June 2026](https://discussions.unity.com/t/coreclr-scripting-and-serialization-update-june-2026/1723299)
- Frame graphs and barriers: [FrameGraph, GDC 2017](https://gdcvault.com/browse/gdc-17/play/1024045),
  [render graphs and Vulkan](https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/),
  [Vulkan barriers explained](https://gpuopen.com/learn/vulkan-barriers-explained/),
  [NVIDIA barrier guidance](https://developer.nvidia.com/blog/advanced-api-performance-barriers/),
  [D3D12 enhanced barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)
- Assets: [glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html),
  [KTX 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html),
  [Basis Universal](https://github.com/BinomialLLC/basis_universal),
  [glTF sample assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
- Shaders: [Slang](https://github.com/shader-slang/slang),
  [Slang at Khronos](https://www.khronos.org/news/archives/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia),
  [Slang reflection](https://shader-slang.org/slang/user-guide/reflection.html),
  [DXC](https://github.com/microsoft/DirectXShaderCompiler),
  [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- Memory, jobs, physics: [VMA 3.4.0](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/),
  [D3D12MA 3.2.0](https://gpuopen-librariesandsdks.github.io/D3D12MemoryAllocator/html/),
  [enkiTS](https://github.com/dougbinks/enkiTS),
  [Taskflow](https://github.com/taskflow/taskflow),
  [Jolt Physics](https://github.com/jrouwe/JoltPhysics)
- Platform and HDR: [SDL3 GPU](https://wiki.libsdl.org/SDL3/CategoryGPU),
  [SDL3 swapchain composition](https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition),
  [HDR on Windows](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
- WebGPU: [specification, Candidate Recommendation Draft](https://www.w3.org/TR/webgpu/),
  [browser support](https://web.dev/blog/webgpu-supported-major-browsers),
  [Dawn](https://dawn.googlesource.com/dawn)
- Profiling, capture, crashes: [Tracy](https://github.com/wolfpld/tracy),
  [RenderDoc in-application API](https://renderdoc.org/docs/in_application_api.html),
  [PIX programmatic capture](https://devblogs.microsoft.com/pix/programmatic-capture/),
  [sentry-native](https://github.com/getsentry/sentry-native),
  [crashpad](https://chromium.googlesource.com/crashpad/crashpad/+/HEAD/README.md)

Not checked against a primary source this time: The Forge's barrier and graph
design, Filament's internal command stream beyond the third-party analysis,
how Bevy 0.19 handles transient aliasing, and Godot's job system.
