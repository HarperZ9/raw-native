# ADR 0011: Motion, a mathematical-animation layer on the web host

**Status:** Accepted. **Date:** 2026-10-09. **Builds on:** [0010](0010-web-host.md).

## Context

The author's direction, 2026-10-09: "Can we extend the animations and make them
more immersive? ... And with our new rendering engine, we should keep extending
it. Like 3B1B on steroids". The explainer films were drawn as still marks over a
GLSL plate. They needed shapes that move by continuous transformation, text and
equations that turn into each other, plots whose data animates, a camera that
moves through scale, and two outputs from one source: a live page you can scrub
and change, and a frame-exact video.

## Decision

**`web/motion/`: a scene layer on the web host, written as creative media.**

- **A scene is a pure function of time.** `frame(t, ctx)` returns a display
  list. Playing, scrubbing, dragging a parameter and an offline render at a
  fixed rate all give the same picture for the same inputs. History layers
  (Threads) are the one exception and are marked as such.
- **Vectors drawn analytically on the GPU.** Paths are flattened on the CPU
  (`path.mjs`, adaptive Bezier and arc flattening) and binned into 16-pixel
  bands (`vector.mjs`). One fragment pass per band walks the band's segments
  once for the winding number and the exact distance to the outline, and turns
  both into coverage (`shaders.mjs`). No tessellation and no MSAA, so a shape
  may change every frame at no extra cost, and the same width of coverage ramp
  gives depth of field: defocus widens the ramp.
- **Morphs by correspondence** (`morph.mjs`): contours pair by class (outlines
  with outlines, holes with holes) and reading order, are resampled by arc
  length, and closed pairs are rotated to the start point of least squared
  distance. Extra contours grow from a point.
- **Text and equations as paths** (`text.mjs`): glyph atlases written by
  `scripts/glyph_atlas.py` from any font whose licence allows it, with HarfBuzz
  pair kerning, and a small TeX subset (groups, scripts, fractions, operators).
  A page with MathJax can feed its SVG path data through `parsePath`.
- **GPU particles** (`particles.mjs`): formations of equal count; the compute
  pass places each particle from time alone.
- **Layers:** Threads (with `present: false`) and Worlds (with a `target`
  view) composite under the vectors, so a scene can sit in a light-thread
  field or fly into a diorama.
- **Two outputs.** `player.mjs` plays a scene live with its sound and captions,
  with scrub, frame step, chapters, parameter sliders and a reduced-motion mode.
  `capture.html` with `scripts/motion_render.py` renders frame i at exactly
  i / fps in headless Chrome and encodes through WebCodecs (the browser's
  hardware encoder), then muxes narration and score with ffmpeg.
- Per ADR 0007 and 0010, Motion has no CPU reference and writes no certificate.
  The pure modules have Node tests (`web/motion/motion.test.mjs`) in CI.

## Consequences

- WebGPU is required for both outputs. The live player shows a notice and the
  page keeps its video when WebGPU is missing.
- Fill cost grows with a band's segment count times its pixel width. Large
  fills with many segments are the expensive case; stroke-only paths are also
  split into 256-pixel chunks.
- Equations cover a subset of TeX. Anything beyond it goes through MathJax's
  SVG output by the page that uses it; nothing is bundled.

## Alternatives

- **Tessellation with MSAA:** standard, but every morph frame would need a new
  tessellation and MSAA gives no defocus.
- **manim itself:** Python and Cairo or OpenGL, offline only, no live page.
- **Raw RGBA readback to ffmpeg:** kept as `--transport raw`; measured about
  0.2 s a frame at 1280 x 720, against about 25 ms a frame at 3840 x 2160
  through WebCodecs on an RTX 4090.

## Reversal signal

A display list whose fill cost dominates the frame on a mid-range laptop at
1080p would argue for a coarse tile pass ahead of the band pass.
