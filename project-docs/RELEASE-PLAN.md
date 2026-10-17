<!-- writing-profile: research -->
# Release plan: an engine people can build games with, and a game people can play

Dated 2026-10-15, written against `main` at a2725e4 (release 0.8.0 of 2026-10-14 plus PR
#143 and #144). Status: plan; four decisions taken (ADR 0018), seven pending. The author's
request, same day: "Continue advancing the
engine and game" and "Really spend your time exploring how to improve it into a full engine
and game release."

Companion documents: [PARITY-MATRIX.md](PARITY-MATRIX.md) scores raw-native against Godot,
Bevy, Unity, Unreal and Defold; [GAME-DESIGN.md](GAME-DESIGN.md) describes the showcase game.
The rendering roadmap stays [docs/architecture/ROADMAP.md](../docs/architecture/ROADMAP.md).

Revised 2026-10-15 after the author's answers to D1, D2, D3 and D10, recorded in
[ADR 0018](../docs/architecture/adr/0018-renderer-first-one-core-buildlang.md): renderer
milestones come first, one C++ core hosts games natively and in the browser, gameplay is
written in BuildLang, and Low Water is the showcase. The audit in section 1 is unchanged from
the first version (PR #147).

Status words follow the repository's practice. **Built**: code exists in a branch.
**Tested**: it has tests or parity checks that pass. **Merged**: on `main`. **Released**: in
a tagged release. **None**: not started.

## 1. Audit: what exists today

Measured on 2026-10-15 from the tree at a2725e4 and the release assets of v0.8.0.

### 1.1 Headline

raw-native is a verified renderer with a strong browser media layer. It is not yet a game
engine. It has no window, swapchain, game loop, gameplay input, audio playback for games,
physics, save system, editor or packaged game runtime. The rendering and look development
work is deep and checked to an unusual standard. Every way a person could play a game made
with it is missing.

Two runtimes have grown side by side:
- **The C++ engine** (23,332 lines in `raw/`, `src/`, `app/`): a CPU reference renderer, an
  RHI over D3D12, Vulkan (compute only), WebGPU and a null device, a frame graph,
  certificates, and a command-line tool. It builds natively and to WebAssembly.
- **The web layer** (17,635 lines of `web/*.mjs`): the Motion timeline player, the sound
  engine, the world modules (scene graph, isometric, tiles, retro 3D), the glTF viewer and the
  style shader modules (CRT, film, dither, dither lighting, painterly and more), each with a
  float64 JavaScript reference and a WGSL pass.

The style passes that define the look live only in the web layer. The native shader library
covers PBR, raster, the software rasteriser, RT, path tracing, SDF marching, shadows and the
M3 post passes. ADR 0018 settles the host: one C++ core, so the style passes port to C++
(milestone N1).

### 1.2 Inventory

| Area | What exists | Evidence | Status |
|---|---|---|---|
| CPU reference renderer | Raster, BVH, ray-traced AO, PBR with nine glTF material extensions, IBL, clustered lights, shadows, GTAO, SSR, TAA, baked lightmaps | `src/renderer/`, M3 rows 2 and 6 to 9 in ROADMAP.md | Released (0.7.0, 0.8.0) |
| RHI and backends | D3D12 (compute, raster, textures, inline ray query, mesh shaders, async compute), WebGPU (raster), Vulkan (compute probe, GEMM, denoiser kernels only), null | `src/rhi/*`, `evidence/hw-h1-*` | Released; Vulkan has no raster path |
| Own rasteriser (RT R1) | Binned compute rasteriser with retro modes, correctly rounded division | `raw/renderer/swr.hpp`, `evidence/rt-r1-bounds.json` | Released; R1 on the RTX is an open failure |
| Path tracer and hybrid RT (R2) | Compute BVH, path tracer with split radiance, hybrid RT shadows and reflections | `evidence/rt-r2-bounds.json` | Released |
| Ray marching (R3) | SDF scene graph, soft shadows, AO, fog, compositing with R1 | `evidence/rt-r3-bounds.json` | Released |
| Hardware RT | DXR 1.1 inline ray query passes H1.1b on the RTX 4090 | `evidence/hw-h1-1b-bounds.json` | Released (0.8.0) |
| Own ray reconstruction (H2) | Trained denoiser v4 on Vulkan kernels | CHANGELOG 0.8.0 and Unreleased | Released with verdict FAIL: worse FLIP and temporal stability than SVGF plus TAA; 11.8 ms against a 2 ms budget |
| Colour and HDR | ACES 2.0 port, tone mappers, PQ and extended sRGB | M1 rows 2 and 3 | Released |
| Post and style passes (web) | CRT, film, palette dither, VHS, painterly, pixel scaling, hysteresis and more | `web/shaders/`, M1 rows 9 and 10 | Released (0.7.0, 0.8.0) |
| Dither-quantised lighting | N-tone and 1-bit lighting, beams, fog to paper white | `web/shaders/ditherlight`, PR #144 | Merged, not released |
| Dither lighting second look | Haze input, new scene | PR #146 | Built and tested, open |
| Paint-over projection, look keys | Projection with keyframe blend, 8 time-of-day slots | PR #145 | Built and tested, open |
| Environment state hub | ADR 0017 | PR #145 | Design only (Proposed) |
| Scene graph (G0) | Hierarchy with `superstack.scene/1` round trip | `web/world/scene.mjs`, M2 criterion 13 | Released (0.7.0), web only |
| 2D and 2.5D | Tiles (100,000-tile check, fuzzed loaders), isometric picking and occlusion fade, pixel scaling, retro 3D | `evidence/m2-*` | Released (0.7.0), web only |
| Assets | glTF loader fuzzed with one million inputs, owned procedural assets | `evidence/m2-gltf-fuzz-ci.json`, `assets/PROVENANCE.md` | Released |
| Animation | Timeline and morph in Motion; no skeletal animation, no state machines | `web/motion/` | Partial, web only |
| Audio | Mixer, live worklet, offline render, loudness meter for films | `web/sound/`, `docs/sound/DESIGN.md` | Released for films; no game playback |
| Game loop and input | None for games. The Motion player reads keys for playback | `web/motion/player.mjs` | None |
| Window and swapchain | None natively | grep for swapchain in `src/`: none | None |
| Physics, save, UI, localisation | None | | None |
| Templates | Three scaffolds rendered as 12-second timelines, not interactive | `templates/`, `tests/web/templates_check.py` | Released as scaffolds |
| Editor | glTF viewer with CPU reference and error map | `web/viewer/` | Released (0.7.0) |
| Platforms shipped | Windows x64 CLI (CPU and D3D12), Linux x64 CLI, WebAssembly, viewer zip | v0.8.0 assets | Released |
| Tests and CI | 58 C++ test files, 17 browser test scripts, 31 Node test files; CI jobs on Ubuntu and Windows: build-test, d3d12 (WARP), fuzz-sanitizers, wasm, identity, shader lab, media | `.github/workflows/ci.yml` | Merged; a full run takes about an hour |
| Licence | FSL-1.1-MIT; superstack vendored; ACES 2.0 ported from OpenColorIO (BSD-3-Clause) | `LICENSE`, `third_party/MANIFEST.md` | Released |

Defects found during the audit:
- The README badge says version 0.5.1; the latest release is 0.8.0.
- The README leads with the AO verifier. An engine release needs a front page that says what
  a game maker gets and how to run a first scene.

### 1.3 What is missing to ship

For a playable game: a native window and swapchain, a game loop, gameplay input with
gamepads, game audio, a dialogue and UI layer, save and load, settings, packaging, crash
reports, and the style passes running in whichever runtime ships.

For an engine people can build with: all of the above as documented API, a scripting or
data-driven gameplay path, an editor that saves scenes, export for Windows, Linux and the
web (macOS by decision), getting-started docs, playable templates and a licence statement a
game maker can rely on. PARITY-MATRIX.md lists each gap against five engines.


## 2. Strategy

The author decided four questions on 2026-10-15 (ADR 0018): "No, renderer first"; "One C++
core"; "BuildLang from the start"; "Yes, Low Water". The strategy follows from them.

1. **Renderer first.** The next milestones finish the renderer on the one core. The game
   milestones follow the ROADMAP milestones that carry their runtime stages (G1 at M3, G2 at
   M4, G3 at M5, G4 at M6). Nothing in the game track moves ahead of a renderer milestone.
2. **One C++ core.** One engine, built natively and to WebAssembly. The WGSL passes are shared.
   The JavaScript modules stay as float64 test oracles, so every port has a reference to match.
3. **BuildLang for gameplay.** Gameplay is BuildLang code from the first game milestone. The
   integration starts as a design study (section 4) and a spike, so the renderer milestones are
   not blocked by it.
4. **Parity, then lead.** Common features reach parity before new techniques get more time.
   Distinctive features (verified rendering, paint-over, dither lighting, environment state)
   continue where Low Water uses them.
5. **Keep what makes the engine trustworthy.** A CPU reference for every rendering feature
   claimed as verified; determinism of the environment state and of replays; bounds committed
   before a measurement; honest failures kept.
6. **Ship without dependencies where practical** (dependency policy, 2026-10-12): own window,
   input, audio and crash code on each platform's system APIs. BuildLang is the author's own
   project and counts as first party. Any outside exception is named with its replacement path.

## 3. Milestones

Three kinds of milestone, in this order:
- **N1 to N3: renderer milestones on the one core.** They come next.
- **The ROADMAP milestones M3 to M6**, unchanged, which carry the runtime stages G1 to G4.
- **W1 to W5: game milestones.** Each starts only after the renderer and ROADMAP milestones it
  names have closed.

The BuildLang track (B0 to B4, section 4) runs beside them and must reach the stage each game
milestone names.

Each milestone closes when every criterion passes and its evidence is committed. No dates, as
in ROADMAP.md. RTX 4090 measurements wait for the GPU lock; software adapters (WARP,
SwiftShader, and a software Vulkan driver for N3) carry every CI check.

| Order | Milestone | Kind | Needs |
|---|---|---|---|
| 1 | N1 Style passes on the one core | Renderer | ADR 0018 |
| 2 | N2 Native presentation | Renderer | N1 for the first presented pass |
| 3 | N3 Vulkan raster for Linux and Steam Deck | Renderer | N2's presentation interface |
| 4 | M3 close (lit scenes, API snapshot, G1) | ROADMAP | N1 to N3 |
| 5 | W1 First walk | Game | M3, B1 |
| 6 | M4 (scale, reach, G2) | ROADMAP | M3 |
| 7 | W2 Systems slice | Game | M4, B2 and B3 |
| 8 | M5 (light transport, G3, packaging) | ROADMAP | M4 |
| 9 | W3 Vertical slice | Game | M5 |
| 10 | W4 Public demo and engine preview | Game | W3 |
| 11 | M6 (G4: BuildLang scripting, editor) | ROADMAP | M5, B4 |
| 12 | E1 Engine 1.0 | Release | M6, W4 |
| 13 | W5 The full game | Game | E1 |

### N1: Style passes on the one core

The style passes live only in `web/shaders/` today. N1 brings them into the C++ engine so the
native and WebAssembly builds draw the same looks.

1. Every pass module under `web/shaders/` has a C++ reference in `src/renderer/` that matches its
   JavaScript float64 reference on a committed fixture set: bit-equal on ordered dither and
   palette decisions, and within a bound stated before the first run for continuous outputs. The
   pass modules are adjacency, caricature, CRT, classic CRT, dither, dither lighting, fibre, film,
   glaze, hatch, hysteresis, lightpaint, paint, paint-over, pixel, purity, sag and VHS. The
   slot-keyed look curves (`web/look/keys.mjs`) port with them.
2. The same WGSL runs through the native shader path on D3D12 (WARP in CI) and through the
   WebAssembly build on WebGPU (SwiftShader in CI). GPU output matches the C++ reference within
   each pass's existing parity bound, with the existing falsification control failing.
3. The JavaScript modules stay as oracles. A CI step runs JS and C++ on the same fixtures and
   fails on any difference beyond the bound.
4. The no-third-party shader check covers the C++ ports.
5. Order inside N1: dither lighting, paint-over and the look curves first (Low Water uses them),
   then film and CRT, then the rest.

### N2: Native presentation

A native window and swapchain, as renderer infrastructure. No game loop or gameplay input; those
are G1 at M3.

1. A renderer host opens a window on Windows 11 with a D3D12 swapchain, handles resize,
   fullscreen and vsync, and exits with code 0. The frame graph owns the present.
2. In CI it presents 600 frames on WARP and writes a frame-hash list that is identical across
   two runs and equal to the offscreen render of the same frames.
3. HDR output through the swapchain matches M1's HDR signal checks (PQ within one 10-bit code)
   read back from the presented image.
4. Frame pacing is recorded: present-to-present intervals over 10,000 frames on the RTX 4090
   when the GPU lock is free, with the bound stated before the run.

Status (2026-10-16). **N2 is closed: every criterion passes.** Criteria 1 to 3 pass on WARP, in CI
and locally, against bounds committed before the first run (`evidence/n2-presentation-bounds.json`,
results in `evidence/n2-presentation-runs.json`). Criterion 4 passes on the RTX 4090: over 10,000
frames at 60 Hz the median interval is 16.663 ms and the 99th percentile 1.088 T, with 4 intervals
above 1.5 T (`evidence/n2-rtx4090/`). The host is `raw_native_present`.

### N3: Vulkan raster for Linux and Steam Deck

1. The RHI gains textures, samplers, graphics pipelines, render passes and a swapchain on Vulkan.
2. The golden identity matrix and the M2 and M3 corpora pass on Vulkan with a software driver in
   CI on Ubuntu, with a golden per device class.
3. The N1 passes pass their parity bounds on Vulkan.
4. A Linux build presents a window (Wayland and X11) and runs the N2 frame-hash check.
5. Steam Deck class: the Linux build and the Windows build under Proton each draw a committed
   scene at 1280 x 800. The p95 frame time is recorded on Deck-class hardware when it is
   available, otherwise the row says "not measured".

Status (2026-10-16). Criteria 1 and 4 pass on lavapipe in CI against bounds committed before the
first run (`evidence/n3-vulkan-raster-bounds.json`, results in `evidence/n3-vulkan-raster-runs.json`):
the RHI on Vulkan with a swapchain, and X11 and Wayland windows running the N2 frame-hash check, whose
frames equal the D3D12 WARP frames hash for hash. Criteria 2, 3 and 5 are open; HDR10 on Vulkan is
not checked (the software drivers offer only sRGB).

Status of criteria 2 and 3 (2026-10-16, `evidence/n3-vulkan-compute-runs.json`): on lavapipe the quick
identity matrix verifies 4 of 4 with a committed golden for the device class, the ten glTF models
verify, the M2 and M3 checks pass their WARP gates, and the style passes pass their ADR 0019 bounds.
Two misses are recorded: lighting shading (3 of 4,096 samples over the bound) and the shadow-map
identity's depth bound on two scenes. Criterion 5 (Steam Deck) is open.

### ROADMAP M3 to M6

Unchanged (`docs/architecture/ROADMAP.md`). M3 carries G1 (loop, input, camera controllers,
picking), M4 carries G2 (audio playback, animation state machines, timelines), M5 carries G3
(the world layer, physics behind an interface, save and load, packaging and crash reports), and
M6 carries G4 (scripting, now BuildLang, and the editor). The open M3 criteria (1 and 3) close
before W1.

PR #148 (open on 2026-10-15) builds a G1 runtime in JavaScript under `web/game/`. Under ADR 0018
the shipping G1 is C++; the JavaScript version can serve as the oracle for the C++ port, as the
shader modules do.

### W1: First walk (after M3 and B1)

1. Lamp Quay is walkable natively and in the browser: click or stick to move on a navigation
   grid, picking, occlusion fade, paint-over projection and look keys.
2. The movement and interaction rules are BuildLang code compiled through B1's path, in both
   builds.
3. A recorded input log replays to the same state hash natively and in WebAssembly, with a
   control log that must differ.
4. p95 frame time at 1920 x 1080 recorded on the RTX 4090 when the GPU lock is free, budget
   16.7 ms stated before the run.

### W2: Systems slice (after M4, B2 and B3)

1. Environment state slice E0 of ADR 0017: the same command log gives the same snapshot bytes
   natively and in WebAssembly; hysteresis tests for every gameplay threshold; the per-frame
   update under 0.1 ms on the reference scene.
2. Dialogue in BuildLang: one conversation with conditions on facts, environment terms and
   instrument readings; a panel readable at 1280 x 800 (Steam Deck text rule, section 5.1).
3. Audio playback on both builds (G2): zone ambience and cues driven by environment events.
4. Blackout: a zone switches between the painted look and dither lighting, with parity as in N1.
5. Hot reload of BuildLang gameplay in native development builds (B3), keeping the game state.

### W3: Vertical slice (after M5)

1. Every item of the vertical-slice definition in GAME-DESIGN.md section 9 passes.
2. Save and load (G3): save, quit, load, and the state hash equals the saved one; save files
   carry a version and a migration test loads the previous version.
3. Settings: resolution, display mode, vsync, frame cap, text size, subtitles, dither strength,
   photosensitivity, audio levels, full rebinding for keyboard and gamepad. A corrupted settings
   file falls back to defaults with a logged message.
4. Crash reports: an unhandled exception writes a minidump (Windows) or a core summary (Linux)
   with the build ID, the BuildLang source line where available, and the last 200 log lines,
   stored locally. Upload is opt in. A deliberate crash test produces a readable report in CI.
5. Packaging: Windows, Linux and web builds from a clean checkout in CI, with SHA256SUMS and
   licence texts.
6. Production rates recorded: words per day, paintings per place, test cost per environment
   rule. The full-game scope in GAME-DESIGN.md section 8 is reset from them.

### W4: Public demo and engine preview

1. The demo is on itch.io (web, Windows, Linux) and on Steam as a demo or playtest, with a store
   page in "Coming Soon" (section 5).
2. Steam Deck: full gamepad play, legible text at 1280 x 800, the on-screen keyboard where text
   entry exists. Valve's compatibility result is recorded, whatever it is.
3. The engine preview (a 0.x release): the renderer host for Windows, Linux and the web, headers,
   the BuildLang bindings, the three templates playable, and a getting-started guide. Three people
   new to the engine each make a first scene in under 30 minutes (target set before the
   sessions).
4. The README leads with what a game maker gets and a run-it-now command; the badge reads the
   current version.
5. No telemetry by default; the demo states what it collects (crash reports only, opt in).

### E1: Engine 1.0 (after M6 and W4)

1. Every row in PARITY-MATRIX.md with the target "parity by 1.0" is at parity.
2. The renderer, scene and runtime APIs are frozen as 1.0 (ADR 0015's full freeze), with a
   compatibility suite and semantic versioning, including the BuildLang binding surface.
3. Platforms: Windows (D3D12), Linux (Vulkan), web (WebGPU). macOS per decision D4.
4. Editor (M6): open, edit and save a scene and its paint-over projectors, edit environment
   curves per slot and weather; saved scenes round-trip through `superstack.scene/1`.
5. A BuildLang module drives a template's gameplay with no engine rebuild (M6 criterion 1).
6. Docs: a manual, an API reference, a BuildLang gameplay guide, a migration guide from 0.x, and
   a licence statement for game makers (D7).

### W5: The full game (after E1)

1. Content per the scope reset at W3.
2. Release on Steam and itch.io with the store requirements in section 5 met.
3. String tables and a font path that can take a second language (D8).
4. The release notes name the engine and BuildLang versions it shipped on.

## 4. BuildLang integration track (design study)

Read on 2026-10-15 from the BuildLang repository's `main` branch (release 1.7.0, tagged that
day): README, USAGE, STATUS, LICENSE, `compiler/src/main.rs` and `compiler/src/runtime/STATUS.md`.
Read only. Nothing was written to the BuildLang repository.

### 4.1 What BuildLang offers today

| Property | Finding | Fit for gameplay |
|---|---|---|
| Production path | `.bld` compiles to C, then the system C compiler builds it (the README calls the C backend "the production execution path"). `buildc build --emit c` keeps the C | Good: the generated C can compile inside raw-native's own build, natively (MSVC, GCC) and with the WebAssembly toolchain, so gameplay rides the same compiler as the engine |
| C interop | `extern "C" fn` exports a stable symbol and `buildc build --emit header` writes the `.h`; `extern "C"` imports call C functions | Good: the engine calls exported gameplay entry points and gameplay calls an engine C header |
| Effects | Typed capability effects: `~ FileSystem`, `~ Network`, `~ Foreign` for `extern` calls, a `Random` capability with a witnessed seed; `buildc check --policy` profiles | Strong fit for a determinism contract: a policy that denies file, network and clock access and allows randomness only from the engine's seed. Gap: every engine call needs `~ Foreign`, which is broad |
| WebAssembly backend | Listed as experimental | Not needed: the C path compiled with the engine's WebAssembly toolchain covers the browser |
| REPL | `buildc repl` type-checks expressions (`:type`); it does not run code | No embedded interpreter exists |
| Runtime library | `compiler/src/runtime/STATUS.md`: the GC, async and FFI modules are designs; no compiled program links a runtime library. The C backend's support is an embedded C header | Gameplay has no GC to schedule; memory is whatever the generated C allocates |
| Watch | `buildc watch --target c` recompiles on change | A base for hot reload: recompile, rebuild a module, reload |
| Licence | BuildLang Fair-Source License 1.0: any purpose except a Competing Use; covers "the BuildLang compiler and toolchain" | Cleared on 2026-10-16: the author decided (D11) that games may ship BuildLang-generated code, the generated C and its embedded header included, under the shipper's terms |
| Known gaps | No `Box<T>` (issue #83, open per the 2026-10-15 state) | Recursive gameplay data (dialogue trees) needs indices or arenas until it lands |

### 4.2 What raw-native needs from it

1. **An embedding contract.** Gameplay is a set of `extern "C"` entry points with fixed
   signatures: `init(state*, seed)`, `step(state*, inputs*, env_snapshot*)` once per fixed step,
   `on_event(state*, event*)`. All gameplay state lives in one block the engine owns, so saving,
   replaying and hot reload can copy it. The engine exposes its queries (`lightAt`, `isWet`,
   `windAt`) and commands (`setWeatherTarget`, `wet`) through a generated C header.
2. **A scoped engine capability.** A named capability (for example `~ Engine`) that allows calls
   into the engine header only, so a gameplay module can be checked to have no other foreign
   access. Today that would need `~ Foreign`. This is a request to the BuildLang project.
3. **A determinism contract with the fixed step.** Gameplay output depends only on (state,
   inputs, environment snapshot, seed). Enforced in three ways: a `buildc check --policy` profile
   that denies file, network, clock and unseeded randomness, run in raw-native's CI; generated C
   compiled with the engine's floating-point rules (no fast-math, no FMA contraction), the same
   rules under which the engine already gets byte-identical output on MSVC, GCC and WebAssembly;
   and a replay test that hashes the state block after every step on each compiler.
4. **Allocation through the engine.** Generated code allocates from an engine-provided arena or
   not at all inside `step`, so frame time stays bounded and state stays inside the block.
5. **Hot reload, native development builds only.** `buildc watch --target c` regenerates C; the
   engine rebuilds the gameplay module as a shared library and swaps it at a frame boundary. The
   state block survives because it is engine-owned; a layout version in the block refuses a swap
   that changes the layout, with a message. No hot reload in shipped or web builds.
6. **Source lines in crash reports.** `-g` exists; the generated C should carry `#line` mappings
   to `.bld` files so a crash report names the BuildLang line.
7. **A licence statement for compiled output.** Done: decision D11, 2026-10-16 (section 7).

### 4.3 Stages

| Stage | Done when | Needed by |
|---|---|---|
| B0 Study | This section, read against BuildLang 1.7.0 | Now (this PR) |
| B1 Embedding spike | A BuildLang module with the three entry points compiles to C and links into raw-native natively (MSVC and GCC) and in WebAssembly; one input log gives the same state hashes on all three | W1 |
| B2 Engine bindings | The engine header is generated from one source, BuildLang declarations for it are generated beside it, and a template's interaction rules call the environment queries | W2 |
| B3 Determinism and reload | The policy profile runs in CI and fails on a module that reads the clock (falsification control); hot reload swaps a module without losing state in a native build | W2 |
| B4 Authoring | M6 criterion 1: a BuildLang module drives a template with no engine rebuild; docs for game makers | M6 and E1 |

Any change BuildLang needs (a scoped capability, `#line` output, `Box<T>`) goes to the BuildLang
project as a request. The study does not edit that repository.

## 5. Distribution

Fetched 2026-10-15 from Valve's Steamworks documentation and itch.io's creator docs (URLs in
the tables). Terms change; recheck before each
submission.

### 5.1 Steam

| Item | Requirement | Source |
|---|---|---|
| Steam Direct fee | USD 100 per product, not refundable, recouped after USD 1,000 adjusted gross revenue. A 30-day wait between paying and release | partner.steamgames.com/steamdirect |
| Revenue share | 30% to USD 10M, 25% to USD 50M, 20% above (moderate confidence: 2018 press coverage; the current agreement text was not fetched) | secondary |
| Payout | Monthly, about 30 days after month end, USD 100 minimum | partner.steamgames.com/doc/finance/payments_salesreporting |
| Store assets | Header 920 x 430, small 462 x 174, main 1232 x 706, vertical 748 x 896 capsules (all required); library assets; at least 5 gameplay screenshots at 1920 x 1080 or more; a trailer | partner.steamgames.com/doc/store/assets, /doc/store/trailer |
| Coming Soon | The page is live at least 2 weeks before release; submit it for review at least 7 business days before the wanted date | partner.steamgames.com/doc/store/coming_soon |
| Review | Store page and build are reviewed separately, typically 3 to 5 business days each, plan for 7. The build must launch on every listed OS and match the listed features | partner.steamgames.com/doc/store/review_process |
| Steam Deck | For "Verified": full controller support by default with matching glyphs, on-screen keyboard for text entry, 30 fps at 800p in default settings, 1280 x 800 preferred, smallest text at least 9 px at 1280 x 800 (12 px recommended), launchers controller-navigable | partner.steamgames.com/doc/steamdeck/compat |
| Steam Input and Cloud | Steam Input API for glyphs and action sets; Steam Cloud through the remote storage API or Auto-Cloud with no code | partner.steamgames.com/doc/features/steam_controller, /doc/features/cloud |
| AI disclosure | The content survey asks for Pre-Generated content (made with AI during development) and Live-Generated content (made at runtime, with guardrails). It covers content that ships: art, sound, narrative, localisation | partner.steamgames.com/doc/gettingstarted/contentsurvey |
| Linux | Native builds on the latest Ubuntu LTS, Vulkan preferred, a Steam Linux Runtime per branch; Windows builds run on Deck through Proton | partner.steamgames.com/doc/store/application/platforms/linux |

What the plan does about it:
- Steam Input and Steam Cloud are optional integrations behind the engine's own input and
  save interfaces, so the itch.io and web builds carry no Steamworks code. Linking the
  Steamworks SDK is a shipped dependency; it stays in a separate store adapter (dependency
  policy exception, named).
- The Deck text rule sets the dialogue panel's minimum text size (GAME-DESIGN.md section 7).
- Earliest Steam timeline from a decision to pay: the fee, 30 days, a Coming Soon page live
  for 2 weeks, reviews of up to 7 business days. Pay the fee at W3 if the author chooses Steam
  (decision D9).

### 5.2 itch.io

| Item | Requirement | Source |
|---|---|---|
| Revenue share | Open revenue sharing: the creator sets 0 to 100%, default 10% | itch.io/updates/open-revenue-sharing |
| Payouts | Direct to the creator's processor or collected by itch.io; funds available 7 days after purchase; USD 5 minimum; a tax interview first | itch.io/docs/creators/payments |
| Web games | A ZIP with `index.html`, at most 1,000 files, 500 MB extracted, 200 MB per file, 240-character paths; limits can be raised through support. SharedArrayBuffer support is not stated, so a threaded WebAssembly build is unverified there | itch.io/docs/creators/html5 |
| Downloads | Per-file limits exist; 1 GB by default per forum posts (moderate confidence) | itch.io/docs/creators/faq |
| butler | Command-line upload to channels with delta patches | itch.io/docs/butler |
| AI disclosure | A disclosure field on the edit page sets an AI tag; expected for games, enforced for asset pages | itch.io/docs/creators/quality-guidelines |

What the plan does about it: itch.io is the first public channel (W4) because it costs
nothing, takes the web build, and needs no SDK. The web build stays single-threaded until
SharedArrayBuffer hosting is checked.

### 5.3 Packaging, crash reports, settings, controllers

- **Packaging.** Windows: a zip with the executable, the data archive, licence texts, an SBOM
  and SHA256SUMS, built in CI from a clean checkout. An installer is optional (Steam and the
  itch.io app install zips). Web: a ZIP inside itch.io's limits. Linux: a tarball built on the
  Ubuntu LTS image once Vulkan raster exists (N3). Code signing for Windows is decision D9.
- **Crash reports.** The engine's own minidump writer on Windows and a signal handler summary
  on Linux, with build ID and the last log lines, stored locally; upload opt in. ROADMAP M5
  names sentry-native (MIT) as an option. The plan prefers an own implementation per the
  dependency policy and keeps sentry-native as the fallback if symbolication costs too much.
- **Settings.** Listed in W3 criterion 3. Settings are a versioned file beside the saves.
- **Controllers.** The system gamepad API on Windows, the Gamepad API on the web, evdev on
  Linux; glyphs per device family; Steam Input on Steam builds. Every action is reachable by
  gamepad, checked by a scripted walkthrough that uses only gamepad events.

## 6. Licensing

- **Engine code.** raw-native is FSL-1.1-MIT (`LICENSE`). Each release becomes MIT two years
  after its publication. That stays as it is; no relicensing is proposed. Vendored and ported
  parts keep their own licences (`third_party/MANIFEST.md`). The licence memory says: keep MIT
  where code is MIT now, and new own work defaults to FSL-1.1.
- **What FSL means for a game maker.** FSL permits any purpose except a Competing Use: making
  the software available in a commercial product that substitutes for it or for another
  product of ours. Shipping a game made with the engine is not a substitute for the engine, so
  it reads as permitted. A game maker should not have to read the licence to know that. The
  plan asks the author to publish a short plain statement (decision D7). This is a reading of
  the licence text, not legal advice.
- **Game content.** Text, art, audio and the game's own code: the author decides (D5). Options
  range from all rights reserved to CC BY for text, per the licence memory's split.
- **Third parties.** No code from ENB, ReShade or any shader author (the no-third-party check
  in PR #145 enforces this for shaders). No test models (2026-10-10 decision). Fonts and any
  outside audio need licences recorded in the manifest before they enter a build.
- **AI disclosure.** Steam requires a disclosure of AI-generated content (section 5). Drafts by
  AI agents count. The store page states it.
- **BuildLang.** The compiler is under the BuildLang Fair-Source License 1.0. It runs at build
  time and is not shipped. Generated C and its embedded header do ship inside a game; the author
  decided on 2026-10-16 that games may ship them under terms of the shipper's choosing (D11,
  section 7).

## 7. Decisions

Decided by the author and recorded in ADR 0018 (D1, D2, D3 and D10 on 2026-10-15; D11 on
2026-10-16):

| ID | Decision | Answer |
|---|---|---|
| D1 | Pull the game-runtime stages ahead of the renderer milestones | "No, renderer first" |
| D2 | Which runtime hosts games | "One C++ core" |
| D3 | How gameplay is authored | "BuildLang from the start" |
| D10 | The showcase game | "Yes, Low Water" (working title) |
| D11 (2026-10-16) | May games ship BuildLang's compiled output (the generated C and its embedded header) | "yes - games may ship buildlang generated code." |

Pending:

| ID | Decision | Recommendation |
|---|---|---|
| D4 | macOS | Defer to after the demo. Options: a Metal backend (ROADMAP M5) or the web build only |
| D5 | The game's repository and content licence | A separate private repository until the demo; content licence chosen by the author |
| D6 | The game's price model | Open; the demo is free either way |
| D7 | A plain statement that games made with raw-native are permitted use under FSL | Yes, in the README and the docs |
| D8 | Languages at launch | English at launch with string tables ready; a second language after release if demand shows |
| D9 | Spend: Steam Direct fee, an optional code-signing certificate | The author's call; the plan works on itch.io with no spend |

## 8. Risks

| Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|
| Renderer first puts the playable slice behind M5 | Certain (accepted with D1) | No player feedback on Low Water for a long stretch | W1 at M3 is a walkable build; outside players see W1 and W2 builds before W3 |
| Porting 18 pass modules to C++ costs more than planned | Medium | N1 slips and everything behind it waits | Port in Low Water's order (dither lighting, paint-over, look curves first); the JS oracles make each port checkable on its own |
| BuildLang lacks a property gameplay needs (scoped capability, `Box<T>`, `#line` output) | Medium | B1 to B3 slip, W1 waits | The study names the gaps now; requests go to the BuildLang project early; arenas and indices stand in for `Box<T>` |
| Floating-point output of generated C differs between MSVC, GCC and WebAssembly | Medium | Replays and saves diverge across builds | The B1 hash test runs on all three from the first spike; the engine's existing compiler rules apply to generated C |
| Two G1 runtimes (PR #148 in JavaScript, the C++ one under ADR 0018) | High | Duplicated work and confusion over which ships | Treat the JavaScript runtime as the oracle for the C++ port; the coordinating session decides what PR #148 becomes |
| Writing quality falls short of the Disco Elysium register | Medium | The game fails even if the engine succeeds | The author owns voice; outside players judge W1 and W2 builds before most content is written |
| Effects research keeps pulling time from N1 to N3 | High | The renderer milestones slip | New effects enter only through a Low Water need |
| A full CI run takes about an hour | High | Merges queue, parallel streams collide | Path filters so a docs or web-only PR skips native jobs; keep each PR small |
| Steam Deck under Proton fails, or Vulkan raster slips | Medium | The Deck review fails | N3 checks both routes early |
| The GPU lock is held for long periods by other work | High | No RTX timings | Software adapters carry correctness; RTX timings are recorded when the lock is free and are never a merge gate |
| Scope of a full game for one developer | High | The game never ships | The slice measures production rates and the scope is reset at W3 |
| Licence uncertainty puts off game makers | Medium | Low adoption | Decision D7 for the engine; D11 settled BuildLang's output on 2026-10-16 |

## 9. Parallel work streams

Each stream owns directories so agents do not edit the same files. Shared files (CMakeLists,
CI workflow, CHANGELOG) change only in small PRs that touch nothing else; a stream that needs
one asks the release stream first.

| Stream | Owns | First deliverable | Depends on |
|---|---|---|---|
| S1 Style-pass ports | `src/renderer/style/` (new), C++ tests for the passes | N1 criteria 1 and 3 for dither lighting, paint-over and look curves | ADR 0018 |
| S2 Native shader path | `src/renderer/gpu/shaders/`, the WGSL shared with `web/shaders/` | N1 criterion 2 | S1's references |
| S3 Presentation | `src/platform/` (new), `app/`, the D3D12 swapchain | N2 | None |
| S4 Vulkan raster | `src/rhi/vulkan/` | N3 | S3's presentation interface |
| S5 Rendering roadmap (existing) | `src/renderer/` outside S1 and S2, `evidence/` | The open M3 criteria | None |
| S6 Runtime stages G1 to G3 | `raw/world/`, `src/world/`, `src/game/` (new) | G1 for M3, in C++ | N2 |
| S7 BuildLang integration | `src/script/` (new), the generated engine header, `tools/buildlang/` | B1 spike | buildc 1.7.0 installed; no writes to the BuildLang repository |
| S8 Environment state | `web/look/`, the C++ environment module under S6's tree by agreement | ADR 0017 E0 in C++ | S6 |
| S9 Game content | the game repository only | Lamp Quay places, characters and BuildLang dialogue | B1, D5 |
| S10 Release engineering | `.github/workflows/`, `scripts/package_*`, crash reporting | CI path filters, then W3 criteria 4 and 5 | S3 |
| S11 Docs, templates and samples | `docs/`, `templates/`, README | W4 criteria 3 and 4 | S1 to S7 |

Rules for every stream: one worktree per branch off `origin/main`; never switch the main
checkout's branch; merge only on green checks with `--match-head-commit`, never with
`--auto`; RTX runs only while holding the GPU lock; no writes outside the stream's own
directories without asking the release stream; no writes to the BuildLang repository from this
plan's streams.

## 10. What this plan does not prove

It is a plan. The parity scores are readings of other engines' public documentation on one
date, not hands-on tests. The BuildLang findings are readings of its repository at 1.7.0, not a
build of the spike. The game concept has not met a player. The scope numbers are targets that
the vertical slice is designed to correct.
