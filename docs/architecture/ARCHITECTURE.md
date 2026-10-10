# raw-native architecture

raw-native is a renderer that proves its own output. It draws a frame, checks
every fast path against a CPU reference, and writes the evidence beside the
pixels. This document sets the architecture it grows into: a consumer renderer,
and possibly a full engine, that keeps that property at every scale.

The decisions behind each part are recorded as ADRs in [adr/](adr/README.md).
The comparison with mature engines is in [GAP-ANALYSIS.md](GAP-ANALYSIS.md) and
the staged plan in [ROADMAP.md](ROADMAP.md). Decisions that belong to the author
are collected in [Decisions for the author](#decisions-for-the-author) and are
marked **Proposed** until the author accepts them. The author accepted A1 to A6 as proposed on 2026-10-04.

## What stays true at every scale

These four properties define the project. Every layer below exists to serve
them, and no change may trade one away without an ADR that says so.

1. **A CPU reference for every feature.** The CPU renderer is the oracle. A
   feature lands in the reference before or with its fast path, and the fast
   path is reconciled against it.
2. **Certificates and receipts on every run.** `raw-cert/2`, `raw-gpu-cert/1`
   and `superstack.receipt/1` record what was claimed, how it was checked and
   which bytes the verdict came from.
3. **Byte-identical determinism where the platform allows it.** The CPU path
   gives the same bytes on MSVC, GCC and WebAssembly. A GPU path gives the same
   bytes on the same backend and device class, and a tolerance verdict across
   classes ([ADR 0006](adr/0006-verification-at-scale.md)).
4. **Every dependency is visible.** The default build depends on the C++
   standard library and one vendored, hash-pinned header. Any change to that is
   an ADR ([ADR 0005](adr/0005-dependency-policy.md)).

## Layers

```mermaid
flowchart BT
  math["math\nvectors, matrices, primitives"]
  core["core\nmemory, images, threads, hashing, handles"]
  platform["platform\nwindows, input, files, time\n(planned)"]
  cert["cert\ncertificates, reconcile, tolerances"]
  scene["scene\nscene description"]
  assets["assets\nglTF, textures, cooking\n(planned)"]
  rhi["rhi\nD3D12, WebGPU, null\n(Vulkan, Metal planned)"]
  graph["graph\nframe graph"]
  renderer["renderer\nCPU reference, GPU renderer"]
  audio["audio\nsound hook\n(planned)"]
  world["world\nECS runtime\n(author decision)"]
  scripting["scripting\nC ABI plugins, scripts\n(author decision)"]
  tools["tools\nCLI, verifier, receipts"]
  editor["editor\nviewer, inspector\n(author decision)"]
  core --> math
  platform --> core
  cert --> core
  scene --> core
  assets --> scene
  rhi --> core
  rhi --> platform
  graph --> rhi
  renderer --> graph
  renderer --> cert
  renderer --> scene
  renderer --> assets
  audio --> platform
  world --> renderer
  world --> audio
  scripting --> world
  tools --> renderer
  editor --> tools
```

An arrow points from a layer to a layer it may include. The table is the rule
of record; `scripts/check_layers.py` holds the same table and CI runs it on
every push.

| Layer | Holds today | May include |
|---|---|---|
| math | `Vec3`, `Mat4`, rays, triangles, AABBs | the standard library only |
| core | arenas and the arena allocator, image buffers and their file formats, row-parallel loops, SHA-256, generational slot pools, version | math |
| platform | nothing yet: windowing, input, files, timers, swapchain surfaces | math, core |
| cert | certificates, the AO reconcile, GPU tolerances | math, core |
| scene | the scene description and the built-in test scene | math, core |
| assets | nothing yet: glTF import, textures, cooked formats | math, core, scene |
| rhi | the interface, and one directory per backend under `src/rhi/` | math, core, platform |
| graph | the frame graph | math, core, rhi |
| renderer | the CPU reference, the GPU renderer, shaders, the GPU reconcile | math, core, platform, cert, scene, assets, rhi, graph |
| audio | nothing yet: the sound hook | math, core, platform |
| world | nothing yet; exists only if the author puts a game runtime in scope | math, core, platform, cert, scene, assets, renderer, audio |
| scripting | nothing yet; same condition | math, core, scene, world |
| tools | CLI parameters, runs, receipts, the verifier, the CLI | every layer above |
| editor | nothing yet; same condition | every layer above, and tools |

Three further rules hold across layers:

- **Graphics and OS API headers stay in their backend.** `d3d12.h`, `dxgi*.h`
  and `windows.h` appear only in `src/rhi/d3d12/` (and a future
  `src/platform/win32/`); `webgpu.h` only in `src/rhi/webgpu/`. The renderer
  cannot reach past the RHI.
- **Public headers include public headers only.** `raw/<layer>/` is the API;
  `src/<layer>/` is private to its layer, and a backend's private headers are
  private to that backend.
- **Vendored code has a named home.** `superstack.hpp` is included from
  `src/tools/` only, today.

Why `cert` sits low: the renderer's frame result carries a reconcile, and every
future layer (assets, world, tools) must be able to issue a certificate without
pulling in the renderer. Why `scene` sits below `renderer`: the scene layer is
the render-facing description (meshes, materials, lights, cameras), as in
Godot's servers and Unreal's scene proxies. A game world, if it comes, sits
above the renderer and extracts into the scene each frame, as Bevy's render
world does ([ADR 0001](adr/0001-layered-architecture.md)).

## The render hardware interface

`raw/rhi/rhi.hpp` is the one boundary between the renderer and a graphics API.
Its object model follows WebGPU and wgpu-hal: a `Device` creates buffers and
pipelines, a `CommandList` records uploads, copies, dispatches and barriers,
and the device submits one list at a time. Objects are generational handles, so
a stale handle fails and can never reach a newer object. The RHI does no hazard
tracking of its own: the frame graph declares every access and hands the RHI
explicit barriers, and each backend maps them to its API or drops them when
its API synchronizes by itself, as WebGPU does.

Version 1 covered buffers, compute pipelines, uploads, copies, dispatches and
read-back. Version 2 (`kRhiVersion` 2, ROADMAP M2 criterion 3) adds RGBA8
textures, samplers, raster pipelines and render passes on D3D12 and WebGPU.
Textures track their own state inside each backend. Raster passes come from
`src/renderer/gpu/shaders/raster.wgsl` through the same WGSL-to-HLSL translator
as the compute passes. A sampled-texture identity check
(`raw_native_cli texture-identity`) compares a GPU render with the CPU sampler
reference in `raw/renderer/sampler.hpp`. Version 3 (ROADMAP M3) adds RGBA16F,
RGBA32F, R32F and Depth32F formats, up to four colour targets and a depth target per
pass, depth tests and culling, and read-only storage buffers in raster stages, so a
vertex shader pulls its own vertices. `raw_native_cli raster-identity` checks the
formats, the depth test and the hardware G-buffer against the CPU rasterizer. The
rasterizer's sub-pixel precision is probed first and emulated by the reference: D3D12
has 8 bits, and SwiftShader has 4. Swapchains, timestamp queries and more
queues arrive behind the same handles when the renderer needs them, with Vulkan
and Metal backends after the author sets platform priority
([ADR 0002](adr/0002-rhi.md)).

### The web host

On the web, `web/raw-gpu.mjs` is the same object model on `navigator.gpu`,
written in JavaScript, with `web/frame-graph.mjs` following the graph rules
below and tested against the C++ graph's golden output. It adds render
pipelines, per-pass timestamps and canvas presentation, and runs creative
modules such as Threads from the same WGSL the D3D12 build runs
([ADR 0010](adr/0010-web-host.md)).

## The frame graph

`raw/graph/frame_graph.hpp` runs every frame, on the GPU and on the CPU. Each
pass declares the resources it reads and writes. The graph then:

1. validates the frame: no resource is read before something writes it, and no
   pass names a resource twice;
2. culls every pass whose results nothing kept reads and that writes no output;
3. places a barrier wherever a buffer's access changes or a write must be
   ordered, batched per pass;
4. on a device, creates only the buffers kept passes use, records the passes
   in declaration order, submits once and waits.

The GPU renderer's passes take their accesses from the binding layout that
`scripts/wgsl_to_hlsl.py` generates from the WGSL, so the barriers follow the
shaders' own declarations and no binding list is written by hand. The CPU
reference runs the same pass names as a host graph. Passes are never reordered,
which keeps the arena's allocation order and every certificate unchanged.
Transient aliasing, async compute, split barriers and textures are the next
graph features ([ADR 0003](adr/0003-frame-graph.md)).

## Public API, ABI and versioning

- The public C++ API is `raw/<layer>/*.hpp`. Anything under `src/` is private.
- Before 1.0, a minor release may break the C++ API. A moved header keeps a
  forwarder at its old path for one minor release: the 0.5 paths
  (`raw/render.hpp` and the rest) forward to their layered paths in 0.6 and are
  removed in 0.7.
- The C++ ABI carries no promise: the library ships as source and as a static
  library built with one toolchain. Binary plugins will use a C ABI with
  versioned function tables, and a plugin built for an older minor keeps
  loading in newer minors, as Godot's GDExtension promises.
- Certificate and receipt schemas are versioned on their own (`raw-cert/2`,
  `raw-gpu-cert/1`, `superstack.receipt/1`) and never change shape within a
  version. A new field is a new version.
- The RHI carries its own `kRhiVersion`, bumped when a backend or caller must
  change.

Details and the 1.0 commitments are in
[ADR 0004](adr/0004-api-abi-versioning.md).

## Plugins

A plugin adds one of four things: an asset importer, a render feature (passes
plus their shaders), an RHI backend or a tool command. It declares an id, a
version, the API version it was built against, its capabilities and its
verification: a render feature brings a CPU reference implementation, or
declares that it has none, in which case its output can only be checked for
tolerance against a reference that does exist and its certificates say
`unverifiable` for identity. First-party features are compile-time plugins.
Binary plugins come later through the C ABI
([ADR 0007](adr/0007-plugin-model.md)).

## Materials

`raw/renderer/pbr.hpp` is the material model: glTF 2.0 metallic-roughness and the ratified
material extensions (ior, specular, clearcoat, sheen, transmission, volume, anisotropy,
iridescence, emissive strength), evaluated in float64. It is the oracle for every GPU shading
path, the way the CPU renderer is the oracle for frames.

- **Energy.** GGX with height-correlated Smith visibility, plus Kulla-Conty multiple scattering
  with coloured Fresnel. The tables it reads (split albedo, an anisotropic split albedo in four
  dimensions, the sheen albedo) are built at start-up by deterministic VNDF sampling and
  quadrature; nothing is copied from another engine's tables.
- **Layers.** Coat over sheen over base. Each layer weights what is below it by a symmetric
  attenuation, so every term stays reciprocal and no white input reflects more than it receives.
- **Checks.** `tests/test_pbr_energy.cpp` integrates the model by a different route from its
  tables (white furnaces, conservation); `tests/test_pbr_reciprocity.cpp` checks reciprocity
  and the iridescence fast path against a spectral reference. `pbr.wgsl` is the float32 form,
  and `raw_native_cli pbr-parity` compares it with the reference on any RHI backend.
- **Two deliberate departures from the extension texts:** iridescence sums eight harmonics
  (the specification sums two, which leaves a 0.32 error on reflective bases at grazing angles),
  and the clearcoat and transmission weights are symmetric (the texts weight by the view angle
  alone, which breaks reciprocity). `Material::specExact` switches a material to the texts'
  iridescence and clearcoat forms, on the CPU and the GPU alike, while the author decides
  which forms are the default (`tests/test_pbr_spec.cpp`). The transmission weight has no
  flag yet. Bounds and every run, failures included:
  `evidence/m3-materials-*.json`.

## Lighting

`raw/renderer/lighting.hpp` is the lighting reference, in view space and physical units.

- **Punctual lights** follow KHR_lights_punctual: directional lights in lux, point and spot
  lights in candela, with the specification's spot falloff and a squared range window. A
  physical camera turns EV100 into exposure.
- **Clusters.** 16 x 9 x 24 view-space froxels with exponential depth slices. Each holds up
  to 256 lights, the clustered path's per-scene limit, so no list is ever cut. The GPU pass
  `light_cluster` builds the same lists, and shading from a list equals shading from every
  light.
- **Image lighting** uses the split sum with the material model's own energy terms. The
  environment is a cube map in a buffer. It is GGX-prefiltered (`light_prefilter`, with
  host-computed sample directions), sampled seamlessly across faces, and read along the
  centroid of the BRDF's lobe. Diffuse light comes from order-2 spherical harmonics. The
  reference is the full material integrated by multiple importance sampling.
- **Shading** of arbitrary surface samples (`pbr_shade`) runs clustered lights and image
  lighting in one pass. `raw_native_cli lighting-parity` checks the clusters, the prefilter
  and the shading against the float64 reference. `raw_native_cli material-gallery` renders
  the showcase: 49 spheres in seven material families, drawn by both paths from the same
  samples. Bounds and every run, failures included: `evidence/m3-lighting-*.json`.

## Baked lighting

`raw/renderer/bake.hpp` is the optional Source-style path (Mitchell, McTaggart and Green 2006).

- **Radiosity normal maps.** Three tangent-space basis vectors per texel. The values are
  normalised to the part of each lobe above the surface and scaled so that their mean
  equals the texel's flat irradiance. A flat normal is then exact up to bake noise, and a
  bumped normal keeps the directional ratios.
- **Ambient cubes** hold six irradiances for models. The important lights are shaded
  directly, as Source does.
- **The baker** is a Lambertian path tracer on `raw/renderer/bvh.hpp`, a binned-SAH BVH
  that returns exactly what a scan of every triangle returns. It is seeded per texel, so a
  bake is identical for any thread count.
- **Checks:** `tests/test_bake.cpp` compares the bake against converged path tracing, after
  a furnace test of the path tracer itself. `raw_native_cli bake-room` renders the showcase
  room both ways. Bounds and every run: `evidence/m3-bake-*.json`.

## Shadows

`raw/renderer/shadows.hpp` holds cascaded shadow maps for the directional light, with the
CPU reference of each GPU pass.

- **Cascades.** Four cascades cover the first 40 units of the view, split by the practical
  scheme (lambda 0.75). Each is an orthographic box around the bounding sphere of its slice,
  so its size never changes as the camera turns, and its origin snaps to whole texels in
  light space, so shadows do not swim. The maps are 1024 square, drawn by the raster pass
  `shadow_depth` with a hardware depth test.
- **Filtering.** Hard shadows take one compare, a one-texel normal offset and a slope bias.
  PCF is a 5 x 5 tent. PCSS searches for blockers over the sun's angular radius (1.5
  degrees), then sizes its kernel by the estimated penumbra; both use Vogel-disc taps.
- **Contact shadows** march 16 steps toward the light over the view depth, up to 0.5 units.
  The compute passes `shadow_lookup` and `shadow_contact` run them on the GPU.
- **Checks.** `tests/test_shadows.cpp` measures swimming directly (where a fixed point lands
  in its texel), and compares hard shadows and PCSS with BVH ray casts toward the sun.
  `raw_native_cli shadow-parity` compares each GPU map, lookup and march with the CPU on
  the same inputs. The lookups and the march pass on WARP and SwiftShader. The map identity
  fails on a few texels per backend: far-plane ties, a sub-texel triangle's depth on WARP,
  and SwiftShader's 4-bit edges. CI gates the parts that pass and prints the rest. Bounds,
  method notes and every run: `evidence/m3-shadows-*.json`.
- **Not yet:** point and spot light shadows, alpha-tested casters, and frame times.

## Screen-space AO, reflections and TAA

`raw/renderer/post.hpp` and `raw/renderer/taa.hpp` hold the CPU references; `post.wgsl` holds
the GPU passes `post_gtao`, `post_ssr` and `post_taa`.

- **GTAO** follows Jimenez et al. 2016: 16 slices uniform around the view vector, 16 steps a
  side within 0.5 units, the cosine-weighted horizon integral, no falloff or bent normal.
  Samples sit exactly on their slice, with depth interpolated as 1 / depth, so an open plane
  reads close to 1.
- **SSR** marches the mirror ray in screen space, clipped to the screen, 64 steps and 8
  bisection steps, with a 0.2-unit thickness.
- **TAA** jitters by Halton(2, 3), reprojects with per-pixel motion, reads history with a
  Catmull-Rom filter, rejects it when the expected depth leaves the 3 x 3 range of the previous
  depths, clips it to a YCoCg box of 1.25 deviations and blends 10% of the new frame.
- **Checks.** `tests/test_post.cpp` compares GTAO and SSR with BVH ray casts on six scenes, and
  `tests/test_taa.cpp` compares TAA with a 16 x 16 supersampled reference.
  `raw_native_cli post-parity` compares the GPU passes with the CPU.
  - GTAO and SSR pass on the open scenes and fail on the dense interior close-up, where screen
    space cannot see behind the pieces.
  - TAA passes its ghosting bound and fails its RMSE bound under a slow pan.
  - GTAO and SSR GPU parity pass on WARP and SwiftShader. TAA GPU parity, each side feeding back
    its own history, fails; from the same history one pixel differs at most.
  - Bounds, eight method notes and every run: `evidence/m3-post-*.json`.

## Verification as the engine grows

- **Every feature has a reference.** The CPU renderer is a layer of the engine,
  not a test fixture, and it runs as a host frame graph with the same pass
  names as the GPU graph.
- **Determinism is scoped and recorded.** CPU output is byte-identical across
  toolchains. GPU output is compared byte for byte only within a device class
  (backend, vendor, architecture, driver), and by tolerance across classes. The
  certificate records the class.
- **Certificates are the golden images.** `evidence/identity-golden.json` holds
  the SHA-256 of every file eleven CPU cases write, and CI checks MSVC and GCC
  against it on every push. The same mechanism extends to GPU device classes on
  a hardware runner and to glTF conformance scenes.
- **Checkers are independent.** `scripts/recheck.py` shares no code with the
  C++ verifier. Each new certificate family gets a second checker of its own.
- **A check proves it can fail.** The layer check and the shader translator
  ship self-tests that feed them violations, and the D3D12 debug layer fails a
  submission on any error it reports.

Details, including how references stay fast enough at 1440p and beyond, are in
[ADR 0006](adr/0006-verification-at-scale.md).

## Contracts: superstack

superstack is the contract raw-native shares with the author's other engines:
canonical JSON, the seed rule, the flick clock, two-verdict receipts, colour and
sound. raw-native vendors its C++ header pinned by hash and runs the shared
vectors in CI. As the engine grows:

- receipts move from the tools layer into `cert` once they stop depending on
  the CLI's parameters;
- the scene description gains a `superstack.scene/1` importer and exporter, so
  any producer can hand raw-native a scene and get a receipt back;
- frame time uses flicks, so frames and sound share one clock;
- the audio layer, when it exists, writes `superstack.sound/1` receipts against
  an offline sample-exact reference, the same pattern as pixels.

## The site's JS engine

The site's media engine (`system/media-engine/` in the site repository) and
raw-native stay separate engines that share contracts. The site engine is a
dependency-free scheduler, plugin host and WebGL2 pool for lightweight pages;
raw-native is a 3D renderer whose WebAssembly builds are already one of the
site engine's plugins, the reference renderer that other plugins are checked
against. That stays the relationship:

- raw-native's wasm builds (CPU, and WebGPU) remain a plugin of the site
  engine, versioned and hash-checked by the loader;
- both sides speak superstack: one scene IR, one seed rule, one clock, one
  receipt;
- raw-native does not take on the site engine's scheduler or DOM concerns, and
  the site engine does not reimplement raw-native's renderer.

A web front end over the wasm build (a viewer that loads glTF and shows its
certificate) is a likely first consumer surface, and it would be a site engine
plugin built on this boundary ([ADR 0008](adr/0008-contracts-and-web.md)).

## Dependencies

Zero dependencies cannot survive a consumer engine. Vulkan and Metal backends
need system SDKs, windowing needs OS libraries, and glTF, image decoding and
texture compression either need vetted libraries or years of reimplementation.
The proposed policy, **a decision for the author**:

> System SDKs, and vetted, vendored, licence-checked libraries, each behind an
> interface the engine owns. Every vendored library is pinned by hash, listed
> with its upstream, version, SPDX licence and local patches, and replaceable
> without touching code outside its adapter. The CPU reference and the
> certificate path stay dependency-free.

The full policy, the candidate libraries and the audit steps are in
[ADR 0005](adr/0005-dependency-policy.md).

## Decisions for the author

All six were accepted as proposed by the author on 2026-10-04.

| ID | Decision | Proposal | Where |
|---|---|---|---|
| A1 | Target platforms and their order | Windows D3D12 and the web (WebGPU) first, as today; Vulkan next (Linux, Steam Deck, Android); Metal after (macOS, iOS) | [ADR 0002](adr/0002-rhi.md) |
| A2 | Dependency policy | The policy above | [ADR 0005](adr/0005-dependency-policy.md) |
| A3 | Scope: renderer, or renderer plus game runtime | Renderer and scene runtime first; the `world` and `scripting` layers stay declared and empty until the author puts ECS, physics and scripting in scope | [ADR 0009](adr/0009-scope.md) |
| A4 | Editor | A verified viewer first (load glTF, render, show the certificate); an editor only after A3 | [ADR 0009](adr/0009-scope.md) |
| A5 | Shader language | Keep the WGSL subset and its translator through the next milestone; decide on Slang with A2 when textures and permutations arrive | [ADR 0002](adr/0002-rhi.md) |
| A6 | API stability | Hold 0.x semantics until the M3 milestone of the roadmap, then promise source compatibility within a major | [ADR 0004](adr/0004-api-abi-versioning.md) |

## Where the first structural step stands

Landed with this document:

- the source tree split into the layers above, with forwarders at the 0.5
  include paths;
- `scripts/check_layers.py`, its self-test, and a CI step that runs both;
- the RHI, extracted from the D3D12 and WebGPU backends, which before this
  each carried their own copy of the pass setup;
- the frame graph, which both the GPU renderer and the CPU reference now run
  through;
- a binding layout generated from the WGSL, which drives the graph's accesses
  and the D3D12 root signatures;
- `scripts/identity_matrix.py` and the golden manifest it checks in CI;
- the D3D12 debug layer as a failing check (`RAW_NATIVE_D3D12_DEBUG=1`), with
  debug names on every buffer and pipeline.

Every certificate, receipt, channel summary and image the renderer writes is
byte-identical before and after this step. The evidence is in the pull request
that landed it.
