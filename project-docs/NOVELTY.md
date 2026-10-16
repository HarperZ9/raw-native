<!-- writing-profile: research -->
# Effects program: novelty record

Dated 2026-10-15. This file backs the word "novel" in EFFECTS-PROGRAM.md. A candidate stays "candidate" until a row here says otherwise. Shader lab 2 keeps its own record for its five techniques (kept outside this repository); this file covers the graphics program only.

Limits, stated once and applying to every row:
- Searches were general web searches (six on 2026-10-15). Google Scholar, ACM DL, IEEE Xplore, GDC Vault and Shadertoy were not queried (lab 2 found the ACM DL and Shadertoy behind bot checks on 2026-10-10; that has not changed here).
- Result pages were read as summaries, not as full texts.
- "No prior found" means a limited search found none. It is not proof of novelty. Public wording stays "new to us" until a direct index search confirms.

## Checked on 2026-10-15

| Technique (as built or proposed) | Verdict | Nearest prior found | What ours adds, and the check |
|---|---|---|---|
| Dither-quantised lighting: light and shadow resolved through an ordered threshold matrix into N tones, 1 bit as the limit case (built, web/shaders/ditherlight) | KNOWN | A Godot 2D "Retro Dithered Lighting and Fog-of-War" shader (smoothstep radial falloff per light, summed, then a Bayer 8x8 threshold and level quantisation). A Godot 1-bit spatial shader whose author notes the quantisation must run after lighting. Return of the Obra Dinn (Lucas Pope, 2018) and its open-source imitations. | Nothing claimed as new. What we add is engineering and measurement: a linear-light fraction so the dither is mean-preserving to half a rank (tested), shaped beam masks with blades, fog to paper white, a G-buffer path so RT shadow rays can feed visibility, CPU and GPU parity, and a motion study. |
| Pattern anchored to the surface with a near-constant on-screen dot size (world-octave option, built) | KNOWN | Dither3D, "Surface-Stable Fractal Dithering", Rune Skovbo Johansen (runevision.com/tech/dither3d, open source): dots stick to surfaces and are added or removed so size stays constant on screen; the added constraint is that shrinking removes dots without adding any. Obra Dinn's sphere-mapped dither (pinned during rotation, not translation). | Ours snaps the cell size by powers of two per pixel from depth. It has seams that Dither3D's fractal construction avoids (seam fraction 1.7% on a dolly) and it costs accuracy (see DITHER-LIGHTING-RESULTS.md). We do not claim it improves on Dither3D. The measured result that screen anchoring wins on accuracy is a finding about our implementation. |
| Bayer against blue noise under motion | KNOWN as a discussion | Pope's devlog (TIGSource, summarised by several articles): Bayer reproduces tone more accurately, blue noise looks more organic and survives scaling and compression better. | Our numbers agree on accuracy (Bayer about 45% lower box-4 error) and add that the two are not separated on temporal metrics. One scene, software metrics only. |
| Paint-over projection: paint over a camera-locked render of block-out meshes and project it back as the surface | KNOWN as camera projection mapping (2.5D matte painting) | Film and VFX practice (SideFX forum threads on camera projection mapping; Texas A&M thesis "Camera-based Texture Mapping" on foreground forms from 2D paintings). Game workflow shown by the DuneCrawl breakdown in the author's inspiration batch. | Hypotheses only: (a) an engine-side export, paint-over and reimport loop with automatic secondary-projector suggestions from visibility (not found in this search); (b) parallax-aware blending of several painted keyframes by camera angle. Both stay "candidate". |
| Palette-locked lighting: light resolved into palette ramps, with palette-cycle animation | KNOWN as pieces | Godot shaders for palette-ordered dithering and 3D post palette modes. A forum workflow that picks the nearest palette colour after adding noise. The same Godot 1-bit shader notes quantising after lighting. | Hypothesis only: lighting evaluated in the palette's own ramp coordinates (hue-shifted ramps from a spectral light) so a lamp changes hue along authored ramps, not just value. Not found in this search. "Candidate". |
| Film halation (red-first), gate weave, emulsion grain | KNOWN | Deep Analog (arXiv 2608.14702, per-channel halation blur radii at 1.4 : 1.0 : 0.7, set empirically); Digital Production's halation article (red layer deepest, warm colour from emulsion structure); Dehancer and Kdenlive gate weave; shader lab 1 in this repository already has a spectral halation and gate weave. | Nothing new claimed for these three. |

## Not yet checked

Anamorphic lens model from cylindrical elements; lens ghost and starburst from a real aperture; emissive lattice with a draw-on parameter; era-faithful low-poly mode (the PS1 look has many shader implementations, so the prior is dense; only a hardware-faithful fixed-point model is a candidate); halftone and line-screen output stage with hysteresis; RT visibility fed through a threshold matrix. Each stays "candidate" in EFFECTS-PROGRAM.md.

## Sources (read 2026-10-15)

- Dither3D: https://runevision.com/tech/dither3d
- Obra Dinn dithering, via summaries: https://blog.playstation.com/archive/2019/10/17/lucas-pope-on-return-of-the-obra-dinns-art-style ; https://www.alanzucconi.com/?p=10365 ; devlog thread named at https://github.com/samuelbigos/godot_dither_shader
- Godot 2D dithered lighting: https://godotshaders.com/shader/2d-retro-dithered-lighting-fog-of-war/
- Godot 1-bit spatial shader: https://godotshaders.com/shader/1-bit-spatial-multi-dithering-shader-with-1-bit-color-palette/
- Camera projection mapping: https://www.sidefx.com/forum/post/350936 ; https://tdl-ir.tdl.org/handle/1969.1/86069
- Halation: https://arxiv.org/pdf/2608.14702 ; https://digitalproduction.com/2024/01/30/halation-and-diffusion-on-the-hunt-for-the-ultimate-film-look/
