# Radiance cascades in flatland

Written 2026-11-06. `web/shaders/cascades` computes 2D global illumination (fluence at every pixel)
with radiance cascades, on the CPU in float64 and on the GPU in WebGPU. Every image is measured
against a brute-force reference of the same scene. The method is A. Sannikov, "Radiance Cascades: A
Novel Approach to Calculating Global Illumination" (working paper, author's public copy). The code
follows the paper's description; no code was taken from any implementation.

## What it does

A scene is a pixel grid of radiance and extinction plus analytic circles (solid emitters and clear
glass of index n). Cascade i has a probe every 2^i pixels, n0 * b^i directions, and holds the
radiance of the interval [t_i, t_i+1] along each direction, with t_i = r0 (b^i - 1) / (b - 1).
Branch b = 2 is the paper's scaling (memory halves per cascade); b = 4 keeps memory constant per
cascade, the scaling the paper reports for its Radiant 2d prototype. Cascades merge top-down by the
paper's equation 13. Cascade 0 sums into fluence per pixel.

Rays walk the grid exactly (each pixel contributes for its true chord) and skip empty space with an
8-level occupancy pyramid. The brute force uses the same tracer with 4096 stratified, jittered rays
per sample, traced to the edge of the frame, with Fresnel-chosen reflection at glass. Every
measured difference therefore comes from the cascades' discretisation.

Two additions:

- **fix**: per-neighbour continuation. Each of the four bilinear neighbours in cascade i+1 gets its
  own ray from this interval's start to that neighbour's interval start. A thin wall between a probe
  and its neighbour then blocks what it should. Cost: four rays per entry.
- **bent rays**: a glass circle turns a ray. The continuation is read at the virtual probe
  q = end - dir_end * t_i+1 in the end direction. For straight rays this is the plain merge
  (`cascades.test.mjs` checks that a glass circle of index 1 changes nothing).

## Running it

```sh
node --test web/shaders/cascades/cascades.test.mjs
python tests/web/cascades.py parity --adapter swiftshader            # CI
python tests/web/cascades.py parity --adapter gpu
python tests/web/cascades.py measure --adapter gpu --scene glass --branch 4 --n0 4 --r0 2 --images out/
python tests/web/cascades.py sweep --adapter gpu --scene room --configs '[{"branch":4,"n0":4,"r0":2}]'
```

## Results on the RTX 4090 (3840 x 2160)

The reference uses 4096 rays per sample on every second pixel (1920 x 1080 samples). Intervals are
95% tile-bootstrap intervals. GPU time is the median and the 5th to 95th percentile of 50 frames
from timestamps. Evidence: `evidence/cascades-rtx4090.json`, `evidence/cascades-sweep-room-rtx4090.json`.

| Scene | b, n0, r0 | Relative RMSE | Reference noise | GPU ms per frame |
|---|---|---|---|---|
| disk (planted) | 2, 4, 4 | 0.107 (0.101 to 0.114) | 0.0027 | 5.81 (5.22 to 6.44) |
| disk (planted) | 4, 4, 2 | 0.022 (0.018 to 0.025) | 0.0027 | 34.7 (31.9 to 36.3) |
| disk (planted) | 4, 8, 4 | 0.013 (0.012 to 0.015) | 0.0027 | 67.4 (64.0 to 69.4) |
| room | 2, 4, 4 | 0.125 (0.117 to 0.133) | 0.0045 | 12.3 (11.6 to 12.9) |
| room | 4, 4, 2 | 0.038 (0.031 to 0.045) | 0.0045 | 69.7 (66.3 to 72.0) |
| room | 4, 8, 4 | 0.025 (0.021 to 0.028) | 0.0045 | 97.6 (88.1 to 133.7) |
| glass | 2, 4, 4 | 0.283 (0.258 to 0.309) | 0.0130 | 5.72 (5.65 to 5.87) |
| glass | 4, 4, 2 | 0.200 (0.178 to 0.222) | 0.0130 | 36.4 (36.0 to 36.6) |
| glass | 4, 8, 4 | 0.173 (0.149 to 0.198) | 0.0130 | 68.0 (66.7 to 68.5) |

Frame times repeat to about 40%. An earlier run of the same settings gave room 49.3 ms at (4, 4, 2)
and 147 ms at (4, 8, 4); both runs are in the evidence file.

At 1920 x 1080 on the room scene: b = 2 (4, 4) 4.22 ms at 0.145; b = 4 (4, 2) 15.2 ms at 0.055;
b = 4 (8, 4) 24.8 ms at 0.038; b = 4 (4, 2) with fix 46.0 ms at 0.043.

**Planted check.** The disk has a closed form, 2 L asin(R / d). The brute force sits 0.0027 from
it, the same as its seed-to-seed noise. The metric applied to the closed form plus 1% of its mean
reads 0.0100. The cascades' error against the closed form equals their error against the brute
force to three digits.

**Branch factor.** At equal cost branch 4 beats branch 2 on every scene in the 40-setting sweep, as
the paper's Radiant 2d note suggested. Branch 2 with r0 = 1 never got under 0.40, whatever n0:
the interval-to-spacing ratio r0 limits it, and raising n0 leaves it in place.

**Thin-wall leak.** Without fix, the room behind a 2-pixel wall reads 1.3 to 3.5 times its reference
brightness at branch 2, and 1.04 to 1.14 at branch 4. With fix it reads 0.94 to 1.02 in every
setting. Fix costs 2.5 to 4.5 times the frame time. At branch 4 without fix, thin bright streaks
still cross the dark room in the render.

**Refractive interface (H150).** Error grows toward the glass interface. Within 2 px of it the RMSE
is 2.7 to 5.4 times the image's (0.81, 0.53 and 0.93 of the image mean for the three settings,
against 0.28, 0.20 and 0.17 overall). Against a reference without Fresnel reflection the bands move
by under 0.03, so the error is the cascades' angular sampling of the bent rays. The deterministic
refraction is a small part of it. The render shows banding inside the rim, a smeared caustic and
soft block-shaped ghosts in open space where bent continuations read coarse cascades. The kill
condition (interface error no higher than the image mean) is not met: angular undersampling does
bite at refractive interfaces.

## Limits

- GPU parity holds on the RTX 4090 and on SwiftShader only because the shader computes sin, cos,
  atan2 and sqrt itself: WGSL's own accuracy bounds let SwiftShader move rays enough to fail.
- Flatland only. The 3D world-space or screen-space extension (ROADMAP P6, raw-hall) is not built.
- Cascade 0 has one probe per pixel; no lower-resolution cascade 0 or temporal reuse is tried, so
  the frame times are the brute-force cost of the method at full resolution.
- The glass reference parity on the GPU is bounded by the share of samples that flip:
  the glass focuses a small emitter, so f32 rounding turns some rays from missing it to hitting it
  (`evidence/shaders-cascades-parity-bounds.json`, amended note).
- The sweep's single-seed reference (2048 rays, every eighth pixel) puts rows near 0.03 at its
  noise floor.
