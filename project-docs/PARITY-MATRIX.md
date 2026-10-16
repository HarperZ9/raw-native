<!-- writing-profile: research -->
# Parity matrix: raw-native against five engines a small team might ship with

Dated 2026-10-15. raw-native is read at `main` a2725e4 (release 0.8.0 plus PR #143 and #144).
The other engines are read from their official documentation and release pages, fetched on
2026-10-15. Each cell is what the fetched page says. Where no page was fetched the cell says
"unknown". These are documentation readings, not hands-on tests.

Versions read: Godot 4.7 (4.7.2, 2026-08-18), Bevy 0.20.0 (2026-10-08), Unity 6.3 LTS
(6000.3.26f1, 2026-10-08; 6.6 is the newest supported stream), Unreal Engine 5.8 (5.8.3,
2026-09-22), Defold 1.14.0 (2026-10-12; year inferred from build dates, moderate confidence).

## How to read the scores

The bar is the common feature: what most of the five engines give a small team shipping a 2D,
2.5D or 3D indie game. **Parity** means raw-native meets that bar in a form a game maker can
use. **Partial** means the capability exists but is not usable from a game runtime, covers
only part of the bar, or runs on one platform. **Missing** means nothing usable exists.

Targets use the release plan's milestones as revised on 2026-10-15 after ADR 0018 (renderer
milestones N1 to N3, game milestones W1 to W5, engine 1.0 as E1; [RELEASE-PLAN.md](RELEASE-PLAN.md)).
Under the standing rule, no common feature is ceded. A row that lands after 1.0 is sequenced
later, with the reason stated. A "lead" is claimed nowhere in this file. Where raw-native has
a property the others' pages do not describe, the row says so and names the test that would
support a claim.

## Matrix

| # | Feature | Godot 4.7 | Bevy 0.20 | Unity 6.3 | Unreal 5.8 | Defold 1.14 | raw-native today | Score | Target |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 3D raster rendering (PBR, lights, shadows) | Forward+, Mobile, Compatibility renderers | Own render graph on wgpu, mesh shaders | URP and HDRP | Nanite, Lumen, MegaLights | 3D supported; PBR manual | PBR with nine glTF extensions, clustered lights, cascaded and contact shadows, GTAO, SSR, TAA, each with a CPU reference. Not in any game runtime; no Vulkan raster | Partial | Parity at N1 and N2 (Windows, web); Linux at N3 |
| 2 | 2D and 2.5D rendering | Full 2D renderer, 2D lights and shadows | Sprites on the 3D renderer, 2D materials | URP 2D Renderer, 2D lights | Paper 2D plugin | 2D-first | Tiles (100,000-tile check), isometric picking and occlusion fade, pixel-perfect scaling, retro 3D; web only. Depth-sorted sprites and 2D lights pending | Partial | Parity at W3 |
| 3 | Real-time global illumination | VoxelGI, SDFGI, lightmaps | Solari path tracer (experimental) | unknown (not fetched) | Lumen | none found | Baked directional lightmaps and ambient cubes; no real-time GI (ROADMAP M5) | Partial | Parity after 1.0 (M5): Low Water uses painted and baked light |
| 4 | Ray tracing | none listed | Solari (experimental), DLSS-RR | HDRP only, DX12, Windows | Hardware RT on DX12; Vulkan RT experimental | none found | Own path tracer, hybrid RT shadows and reflections, SDF marching, DXR inline ray query; own ray reconstruction fails its bounds. Not in a game runtime | Partial | Parity at E1 as an optional path with a raster fallback |
| 5 | Post effects and stylised looks | Built-in post stack | Built-in post | Built-in post | Built-in post | Render scripts and shaders | CRT, film, palette dither, dither-quantised lighting, VHS, painterly, pixel scaling, look keys; each with a CPU reference and GPU parity | Parity (web only today) | Native at N1. Any "better than" claim waits for blind comparisons (ROADMAP's shader rule) |
| 6 | Input with gamepad and rebinding | Remappable actions, up to 8 gamepads | Gamepads via gilrs; no first-party rebinding found | Input System with runtime rebinding | Enhanced Input with remapping | Gamepads via mapping file; no rebinding API found | Motion player keys only | Missing | Parity at W1 (devices), W3 (rebinding) |
| 7 | Audio for games | 2D and 3D positional, buses, effects | bevy_audio playback | Built-in (detail not fetched) | Built-in; WASAPI default on Windows | Sound components, streaming | Mixer, live worklet, offline render and loudness meter, built for films; no game playback, no positional audio | Partial | Parity at W2 (playback, zones); positional at E1 |
| 8 | Animation | AnimationTree, state machines, blend spaces, skeletons, IK | Skeletal, blending, morph targets | unknown (not fetched) | Skeletal, Control Rig, State Tree, Sequencer | Flipbooks, baked skinned glTF, tweens | Timelines, morph and particles in Motion; no skeletal animation, no state machines | Partial | Flipbooks and tweens at W3; skeletal and state machines at E1 (ROADMAP M4 and G2) |
| 9 | Physics | Jolt (3D default), Godot Physics (2D) | none first-party | PhysX (3D), Box2D (2D) | Chaos | Box2D (2D), Bullet (3D) | None | Missing | Collision queries and a character controller at W3; 2D rigid bodies at E1; 3D rigid bodies after 1.0, behind the ROADMAP's physics interface. Low Water needs only queries |
| 10 | Game UI | Control nodes, themes, rich text | ECS UI with flexbox, Feathers widgets | UI Toolkit, uGUI | UMG, Slate, Common UI | GUI component | Vector and text drawing with a TeX subset (M1); no widgets, layout or focus navigation | Partial | Parity at W3 (widgets, layout, gamepad focus, text scale) |
| 11 | Scenes and prefabs | Nested reusable scenes | Scenes with instancing and hot reload | Prefabs (not fetched) | Blueprint classes (not fetched) | Collections and game objects | Scene hierarchy with `superstack.scene/1` round trip, web only; no prefab instancing | Partial | Parity at W2 (native scene, instancing) |
| 12 | Asset import | glTF, .blend, FBX, OBJ, images, audio; live reload | glTF, many image formats, hot reload of any asset | glTF unknown (not fetched) | Interchange: glTF, USD, FBX | glTF; hot reload of scripts, GUI, shaders | glTF loader fuzzed with one million inputs, PNG, LDtk and TMX loaders; no hot reload, no audio import pipeline | Partial | Hot reload at W2; audio import at W2 |
| 13 | Gameplay scripting | GDScript, C#, GDExtension | Rust | C# | C++, Blueprints | Lua | None; the C ABI waits for a consumer (ADR 0007) | Missing | BuildLang gameplay from W1 (ADR 0018); hot reload at W2; authoring with no rebuild at E1 (M6) |
| 14 | Editor | Full, mature | No official editor yet | Full, mature | Full, mature | Full | glTF viewer with CPU reference and error map | Missing | Scene, paint-over and environment editing at E1 |
| 15 | Export: Windows | Yes | Yes | Yes | Yes | Yes | CLI builds (CPU and D3D12) | Partial | Game builds at W3 |
| 16 | Export: Linux | Yes | Yes | Yes | Yes | Yes | CLI build only; Vulkan has no raster path | Partial | Native Linux and the Proton route at N3 |
| 17 | Export: macOS | Yes | Yes | Yes | Yes | Yes | None | Missing | Decision D4; the web build covers macOS browsers at W3 |
| 18 | Export: web | WebGL 2 (Compatibility renderer) | WebGL 2 or WebGPU | Yes | No official export since 4.24 | HTML5 | WebAssembly with WebGPU; the viewer runs in browsers | Partial | Renderer build at N1; game build at W1 |
| 19 | Export: mobile | iOS, Android | iOS, Android | iOS, Android | iOS, Android | iOS, Android | None (the web viewer runs on phones) | Missing | After 1.0; Low Water targets desktop, Steam Deck and the web |
| 20 | Export: consoles | Through third-party porters | not mentioned | Through platform NDAs | Through platform SDKs | Manuals behind platform holders | None | Missing | After 1.0; needs platform agreements the author does not hold |
| 21 | Save and load | FileAccess, JSON, ConfigFile | Scene serialisation; no save API found | unknown (not fetched) | SaveGame system | `sys.save`, `sys.load` | None | Missing | Parity at W3 (G3 at M5), with versioned save files |
| 22 | Localisation | CSV and gettext, plurals, BiDi, RTL, pseudolocalisation | none first-party | Localization package | Localization tools | Community modules only | None (one Latin font atlas) | Missing | String tables at W3; BiDi and a second script at W5 |
| 23 | Accessibility | Screen readers on desktop, UI scaling, remapping | AccessKit (screen readers experimental on Unix) | Screen readers incl. desktop since 6.3 | Screen readers (experimental), remapping, subtitles (beta) | none found | Caption toggle in the film player only | Missing | Text scale, subtitles, remapping, photosensitivity at W3; screen reader at E1 |
| 24 | Profiling | Visual profiler, Tracy, Perfetto | Tracy, Chrome tracing | Profiler | Unreal Insights | On-screen and web profilers | Per-pass GPU timestamps, frame and pass budgets with a CI gate; Tracy zones planned (M4) | Partial | Parity at W4 (in-game overlay and trace export) |
| 25 | Verification of rendering | none described | none described | none described | none described | none described | CPU reference for every verified pass, certificates, byte-identical frames per device class, bounds committed before runs | Present (distinctive) | Keep. A claim that others lack it needs a survey beyond these pages |
| 26 | Docs | Full versioned docs | Docs "can be sparse" (Bevy's own words) | Versioned manual | Versioned docs | Manual per feature | Deep design records, ADRs, evidence; no user manual or getting-started guide | Partial | Getting started at W4; manual and API reference at E1 |
| 27 | Samples and templates | unknown (not fetched) | Examples in repository | unknown (not fetched) | unknown (not fetched) | unknown (not fetched) | Three template scaffolds as timelines, not interactive | Partial | Three playable templates at W4 |
| 28 | Licence clear for commercial games | MIT, no fees | MIT or Apache 2.0 | Free under USD 200,000; Pro above | Free under USD 1M; 5% royalty above (3.5% with the Epic launch programme) | Defold License, no fees or royalties | FSL-1.1-MIT: games read as permitted use; each release becomes MIT after two years; no plain statement for game makers | Partial | Parity at W4 with a plain statement (release plan D7) |

## Counts

| Score | Rows | Count |
|---|---|---|
| Parity | 5 (web only today) | 1 |
| Present, distinctive | 25 | 1 |
| Partial | 1, 2, 3, 4, 7, 8, 10, 11, 12, 15, 16, 18, 24, 26, 27, 28 | 16 |
| Missing | 6, 9, 13, 14, 17, 19, 20, 21, 22, 23 | 10 |

## The five gaps that matter most

Ranked by what blocks a person from playing a raw-native game, and then from making one.

1. **No runtime host (rows 6, 15, 18).** No window, game loop or gameplay input. Nothing built
   with the engine can be played today. Target N2 (window and swapchain), then W1 after M3 (G1).
2. **No game UI, save or settings (rows 10, 21, 23).** A dialogue-led game cannot ship
   without readable panels, gamepad focus, saves and access settings. Target W2 and W3.
3. **No authoring path (rows 13, 14).** Game makers need a way to change gameplay and scenes
   without rebuilding the engine. Target W1 (BuildLang gameplay) and E1 (editor).
4. **Platform reach (rows 16, 17, 19).** Linux lacks a raster path on Vulkan; macOS and mobile
   are absent. Steam Deck needs Linux or Proton. Target N3.
5. **Physics and animation (rows 8, 9).** All five engines ship both. Low Water needs little of
   either, which is why they come later, but a general engine release needs them. Target E1,
   with 3D rigid bodies after 1.0.

## Where raw-native is different, and what a claim would need

- **Verified rendering (row 25).** No page read on 2026-10-15 describes a CPU reference and
  certificate per rendering feature. That is an absence in the pages read, not a survey. A
  claim needs a check of each engine's test suites.
- **Stylised looks with references (row 5).** The passes match their own float64 references.
  "Better than" needs the blind comparisons the shader rule asks for.
- **Paint-over projection and the environment state hub.** No row for them exists because no
  other engine was checked for them as features. NOVELTY.md records paint-over as known
  technique (camera projection mapping).

## Sources

All fetched 2026-10-15. Godot: godotengine.org/download/archive, docs.godotengine.org
(list_of_features, using_jolt_physics, animation_tree, exporting_for_web), godotengine.org/consoles,
godotengine.org/license. Bevy: github.com/bevyengine/bevy/releases, bevy.org/news (0.20 post and
sixth birthday post), bevy.org, docs.rs/bevy 0.20.0, bevy.org/learn/quick-start/introduction.
Unity: the Unity release API, unity.com/releases/unity-6, unity.com/products/pricing-updates,
unity.com/pricing, the 6000.3 manual (accessibility, render pipeline comparison, platforms,
physics, what's new in 6.3, UI toolkits), the Input System 1.14 and Localization 1.5 package
manuals. Unreal: unrealengine.com/license and the EULA, the 5.8 release notes, the 5.8 and
5.8.3 forum announcements, the documentation pages on hardware ray tracing, Paper 2D, screen
readers, Enhanced Input, physics, saving and loading, localisation, Unreal Insights and sharing
and releasing projects; web export status from secondary sources (moderate confidence).
Defold: github.com/defold/defold/releases, defold.com/license, the Defold manuals
(introduction, physics, hot reload, importing models, gamepads, file access, profiling,
bundling); localisation from community pages because no official manual was found.
