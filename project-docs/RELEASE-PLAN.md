<!-- writing-profile: research -->
# Release plan: an engine people can build games with, and a game people can play

Dated 2026-10-15, written against `main` at a2725e4 (release 0.8.0 of 2026-10-14 plus PR
#143 and #144). Status: proposal. The author's request, same day: "Continue advancing the
engine and game" and "Really spend your time exploring how to improve it into a full engine
and game release."

Companion documents: [PARITY-MATRIX.md](PARITY-MATRIX.md) scores raw-native against Godot,
Bevy, Unity, Unreal and Defold; [GAME-DESIGN.md](GAME-DESIGN.md) proposes the showcase game.
The rendering roadmap stays [docs/architecture/ROADMAP.md](../docs/architecture/ROADMAP.md);
this plan adds a release track beside it and asks to pull some runtime stages forward
(section 3).

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
M3 post passes. Choosing which runtime hosts a game is the first decision (D2, section 6).

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

1. **The game drives the engine.** Every engine feature in the release track exists because
   the vertical slice of Low Water needs it, or because PARITY-MATRIX.md marks it as a common
   feature no engine user should miss. This follows ADR 0014's rule: a template that needs a
   runtime feature scheduled two milestones later pulls it forward.
2. **Playable early.** A walkable greybox arrives at R1 and a playable slice at R3, before the
   remaining rendering criteria close.
3. **Parity first, then lead.** As with Flywheel (2026-10-14 decision), common features reach
   parity before new techniques get more time. Distinctive features (verified rendering,
   paint-over, dither lighting, environment state) continue only where the game uses them.
4. **Keep what makes the engine trustworthy.** A CPU reference for every rendering feature
   claimed as verified; determinism of the environment state and of replays; bounds committed
   before a measurement; honest failures kept.
5. **Ship without dependencies where practical** (dependency policy, 2026-10-12): own window,
   input, audio and crash code on each platform's system APIs. Any exception is named with
   its in-house replacement path.

## 3. Milestones

The R track runs beside the rendering roadmap. Each milestone closes when every criterion
passes and its evidence is committed. No dates, as in ROADMAP.md. RTX 4090 measurements wait
for the GPU lock; software adapters (WARP, SwiftShader) carry every CI check.

How the R track moves the rendering roadmap (to be recorded in ADR 0018 if the author
accepts, decision D1):

| Runtime stage | ROADMAP milestone today | Needed by | Move |
|---|---|---|---|
| G1 loop, input, camera, picking | M3 | R1 | Forward |
| Native window and swapchain | M4 | R1 | Forward |
| G2 audio playback, timelines | M4 | R2 | Forward |
| G3 save and load, world layer | M5 | R2 (save), R3 (world) | Forward |
| Packaging and crash reports | M5 | R3 | Forward |
| G4 scripting and editor | M6 | R5 | Unchanged |
| Open rendering criteria (M3 1 and 3; M4 scale; M5 GI and Metal) | M3 to M5 | Not on the game's path | Continue in parallel |

### R0: Decisions recorded

1. This plan is merged.
2. ADR 0018 (game runtime host and the R track) is written and carries the author's dated
   decision on D1 and D2.
3. ADR 0017 is accepted or amended by the author.
4. The game repository exists with the author's licence choice (D5); the engine repository
   keeps templates only.

### R1: First walk (a playable greybox)

1. A runtime host opens a window on Windows 11 with a D3D12 swapchain, handles resize,
   fullscreen and vsync, and exits with code 0. In CI it runs 600 frames on WARP and writes a
   frame-hash list that is identical across two runs.
2. The same scene runs in a browser through the WebGPU build. Frames match the native frames
   within the existing parity bounds on SwiftShader and WARP.
3. A fixed-step loop at 60 Hz with render interpolation. Input from keyboard, mouse and
   gamepad (Windows system gamepad API natively; the Gamepad API on the web). A recorded input
   log replays to the same state hash on both hosts, with a control log that must differ.
4. Lamp Quay (the block-out of PR #145) is walkable: click or stick to move on a navigation
   grid, picking, occlusion fade.
5. Paint-over projection and look keys run inside the runtime, within PR #145's parity bounds
   against the CPU reference.
6. p95 frame time at 1920 x 1080 recorded on the RTX 4090 when the GPU lock is free, with the
   budget (16.7 ms) stated before the run.

### R2: Systems slice

1. Environment state slice E0 of ADR 0017: the same command log gives the same snapshot bytes
   natively and in WebAssembly; hysteresis tests for every gameplay threshold; the per-frame
   update costs under 0.1 ms on the reference scene (ADR 0017's own reversal trigger).
2. Dialogue: a text format and interpreter, one conversation with conditions on facts,
   environment terms and instrument readings; a dialogue panel readable at 1280 x 800 (body
   text height per the Steam Deck guidance in section 4).
3. Audio playback on both hosts: zone ambience and cues driven by environment events through
   the existing mixer design; native output on the system audio API.
4. Save and load: save, quit, load, and the state hash equals the saved one. Save files carry
   a version and a migration test loads the previous version.
5. Blackout: a zone switches from the painted look to dither lighting and back, with GPU
   parity against the CPU reference as in PR #144.

### R3: Vertical slice (game alpha)

1. Every item of the vertical-slice definition in GAME-DESIGN.md section 9 passes.
2. Settings: resolution, display mode, vsync, frame cap, text size, subtitles, dither
   strength, photosensitivity, audio levels, full rebinding for keyboard and gamepad. Settings
   persist and a corrupted settings file falls back to defaults with a logged message.
3. Crash reports: an unhandled exception writes a minidump (Windows) or a core summary
   (Linux) with the build ID and the last 200 log lines, stored locally. Upload is opt in, off
   by default. A deliberate crash test produces a readable report in CI.
4. Packaging: a Windows zip and a web build from a clean checkout in CI, with SHA256SUMS and
   licence texts.
5. Production rates recorded from the slice: words per day, paintings per place, test cost per
   environment rule. The full-game scope in GAME-DESIGN.md section 8 is reset from them.

### R4: Public demo and engine preview

1. The demo is on itch.io (web and Windows) and on Steam as a demo or a playtest, with a
   store page in "Coming Soon" (Steam rules in section 4).
2. Steam Deck: the demo runs through Proton or as a native Linux build, with full gamepad
   play, legible text at 1280 x 800 and the on-screen keyboard where text entry exists. The
   result of Valve's compatibility review is recorded, whatever it is.
3. The engine preview (a 0.x release): the runtime host for Windows and the web, headers, the
   three templates playable (not timelines), and a getting-started guide. Three people who
   have not used the engine each make a first scene in under 30 minutes (target set before
   the sessions).
4. The README leads with what a game maker gets and a run-it-now command; the badge reads the
   current version.
5. No telemetry by default; the demo states what it collects (crash reports only, opt in).

### R5: Engine 1.0

1. Every row in PARITY-MATRIX.md with the target "parity by 1.0" is at parity.
2. The renderer, scene and runtime APIs are frozen as 1.0 (ADR 0015's full freeze), with a
   compatibility suite and semantic versioning from then on.
3. Platforms: Windows (D3D12), Linux (Vulkan with a raster path), web (WebGPU). macOS per
   decision D4.
4. Editor (M6 scope): open, edit and save a scene and its paint-over projectors (export the
   render, reimport the painting), edit environment curves per slot and weather, and the saved
   scene round-trips through `superstack.scene/1`.
5. Gameplay authoring per decision D3 drives a template with no engine rebuild.
6. Docs: a manual, an API reference generated from the headers, a migration guide from 0.x,
   and a licence statement for game makers (decision D7).

### R6: The full game

1. Content per the scope reset at R3.
2. Release on Steam and itch.io with the store requirements in section 4 met.
3. Localisation: at least the string tables and a font path that can take a second language,
   even if the launch ships English only (decision D8).
4. The game's release notes name the engine version it shipped on.

## 4. Distribution

Fetched 2026-10-15 from Valve's Steamworks documentation and itch.io's creator docs (URLs in
the tables). Terms change; recheck before each
submission.

### 4.1 Steam

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
  for 2 weeks, reviews of up to 7 business days. Pay the fee at R3 if the author chooses Steam
  (decision D9).

### 4.2 itch.io

| Item | Requirement | Source |
|---|---|---|
| Revenue share | Open revenue sharing: the creator sets 0 to 100%, default 10% | itch.io/updates/open-revenue-sharing |
| Payouts | Direct to the creator's processor or collected by itch.io; funds available 7 days after purchase; USD 5 minimum; a tax interview first | itch.io/docs/creators/payments |
| Web games | A ZIP with `index.html`, at most 1,000 files, 500 MB extracted, 200 MB per file, 240-character paths; limits can be raised through support. SharedArrayBuffer support is not stated, so a threaded WebAssembly build is unverified there | itch.io/docs/creators/html5 |
| Downloads | Per-file limits exist; 1 GB by default per forum posts (moderate confidence) | itch.io/docs/creators/faq |
| butler | Command-line upload to channels with delta patches | itch.io/docs/butler |
| AI disclosure | A disclosure field on the edit page sets an AI tag; expected for games, enforced for asset pages | itch.io/docs/creators/quality-guidelines |

What the plan does about it: itch.io is the first public channel (R4) because it costs
nothing, takes the web build, and needs no SDK. The web build stays single-threaded until
SharedArrayBuffer hosting is checked.

### 4.3 Packaging, crash reports, settings, controllers

- **Packaging.** Windows: a zip with the executable, the data archive, licence texts, an SBOM
  and SHA256SUMS, built in CI from a clean checkout. An installer is optional (Steam and the
  itch.io app install zips). Web: a ZIP inside itch.io's limits. Linux: a tarball built on the
  Ubuntu LTS image once Vulkan raster exists. Code signing for Windows is decision D9.
- **Crash reports.** The engine's own minidump writer on Windows and a signal handler summary
  on Linux, with build ID and the last log lines, stored locally; upload opt in. ROADMAP M5
  names sentry-native (MIT) as an option. The plan prefers an own implementation per the
  dependency policy and keeps sentry-native as the fallback if symbolication costs too much.
- **Settings.** Listed in R3 criterion 2. Settings are a versioned file beside the saves.
- **Controllers.** The system gamepad API on Windows, the Gamepad API on the web, evdev on
  Linux; glyphs per device family; Steam Input on Steam builds. Every action is reachable by
  gamepad, checked by a scripted walkthrough that uses only gamepad events.

## 5. Licensing

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
- **AI disclosure.** Steam requires a disclosure of AI-generated content (section 4). Drafts by
  AI agents count. The store page states it.

## 6. Decisions for the author

| ID | Decision | Recommendation |
|---|---|---|
| D1 | Adopt the R track and move G1, the native window, G2, save and load, packaging and crash reports forward (section 3) | Yes; record in ADR 0018 |
| D2 | Which runtime hosts games | One C++ core (gameplay, environment state, dialogue) built natively and to WebAssembly; native hosts on D3D12 and Vulkan; the web host on WebGPU; the WGSL passes shared, with their JavaScript references kept as test oracles. The alternative, a JavaScript runtime with desktop builds in the system web view, is faster to a web demo but makes Steam Deck and Linux depend on web view WebGPU support, which is unverified |
| D3 | Gameplay authoring | Data-first now (text files for places, people, dialogue, rules); a scripting language through the C ABI later. Candidates: the author's own buildlang, or a small vendored interpreter. Decide at R4 |
| D4 | macOS | Defer to after the demo. Options: a Metal backend (ROADMAP M5) or the web build only |
| D5 | The game's repository and content licence | A separate private repository until the demo; content licence chosen by the author |
| D6 | The game's price model | Open; the demo is free either way |
| D7 | A plain statement that games made with raw-native are permitted use under FSL | Yes, in the README and the docs |
| D8 | Languages at launch | English at launch with string tables ready; a second language after release if demand shows |
| D9 | Spend: Steam Direct fee, an optional code-signing certificate | The author's call; the plan works on itch.io with no spend |
| D10 | The showcase concept | Low Water (GAME-DESIGN.md), with its working title |

## 7. Risks

| Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|
| Writing quality falls short of the Disco Elysium register | Medium | The game fails even if the engine succeeds | The author owns voice; the slice is judged by outside players before more content is written |
| Porting the style passes to the native host costs more than planned | Medium | R1 or R2 slips | The WGSL is shared; only pass drivers port. Web host first if native slips (ROADMAP A1 already puts web first) |
| Effects research keeps pulling time from runtime work | High | No playable build | The R track has its own stream; new effects enter only through a game need |
| A full CI run takes about an hour | High | Merges queue, parallel streams collide | Path filters so a docs or web-only PR skips native jobs; keep each PR small |
| Steam Deck support through Proton fails or the native Linux path lacks raster on Vulkan | Medium | The Deck review fails | Raster on Vulkan is in the R5 scope; test the Proton route at R3 |
| The GPU lock is held for long periods by other work | High | No RTX timings | Software adapters carry correctness; RTX timings are recorded when the lock is free and are never a merge gate |
| The paint-over pop and seam misses (PR #145) show in play | Medium | Visible artefacts on pans | Limit the camera pan range to the measured coverage; keep the pop metric as a tracked bound |
| Scope of a full game for one developer | High | The game never ships | The slice measures production rates and the scope is reset at R3 from them |
| Licence uncertainty puts off game makers | Medium | Low adoption | Decision D7 |

## 8. Parallel work streams

Each stream owns directories so agents do not edit the same files. Shared files (CMakeLists,
CI workflow, CHANGELOG) change only in small PRs that touch nothing else; a stream that needs
one asks the release stream first.

| Stream | Owns | First deliverable | Depends on |
|---|---|---|---|
| S1 Runtime host | `src/platform/` (new), `app/`, the CMake game target | R1 criteria 1 to 3 | D2 |
| S2 Render passes in the runtime | `src/renderer/gpu/shaders/`, `web/shaders/` pass drivers | R1 criterion 5, R2 criterion 5 | S1's frame interface |
| S3 Environment state | `raw/world/`, `src/world/` (new), `web/look/` | R2 criterion 1 | ADR 0017 accepted |
| S4 Gameplay systems | dialogue, instruments, Ledger, save (`src/game/`, new) | R2 criteria 2 and 4 | S3's snapshot API |
| S5 Audio | `web/sound/`, `src/audio/` (new) | R2 criterion 3 | S1 |
| S6 Game content | the game repository only | Lamp Quay places, characters and dialogue | S4's text format |
| S7 Release engineering | `.github/workflows/`, `scripts/package_*`, crash reporting | R3 criteria 3 and 4 | S1 |
| S8 Docs, templates and samples | `docs/`, `templates/`, README | R4 criteria 3 and 4 | S1 to S4 |
| S9 Rendering roadmap (existing) | `src/renderer/` outside S2's files, `evidence/` | The open M3 criteria | None |

Rules for every stream: one worktree per branch off `origin/main`; never switch the main
checkout's branch; merge only on green checks with `--match-head-commit`, never with
`--auto`; RTX runs only while holding the GPU lock; no writes outside the stream's own
directories without asking the release stream.

## 9. What this plan does not prove

It is a plan. The parity scores are readings of other engines' public documentation on one
date, not hands-on tests. The game concept has not met a player. The scope numbers are
targets that the vertical slice is designed to correct.
