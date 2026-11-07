# Radiance cascades in 3D (world space)

Written 2026-11-06. `web/shaders/cascades3d` computes diffuse global illumination (sky and every
interreflection) for triangle scenes with world-space radiance cascades, on the CPU in float64 and
on the GPU in WebGPU. Every image is measured against the repository's path tracer on the same
triangles. Method: A. Sannikov, "Radiance Cascades: A Novel Approach to Calculating Global
Illumination" (working paper, author's public copy), extended to 3D world probes as the paper
describes. ROADMAP P6 cites the split-cascades work of Freeman, Sannikov and Pitt-Francis (2026);
that paper was not read for this build. Irradiance-probe practice (a normal bias and Chebyshev depth
visibility) follows the published DDGI method (Majercik et al. 2019). The traced segment near glass
is S. Caires's suggestion in the reply thread to C. Bennett's analytic-circle test. The code follows
the descriptions; no code was taken from any implementation.

## What it does

A pixel is `emission + albedo / pi x (E_sun + E_gi)`. The shadowed sun is lighting model 2's; the
cascades give `E_gi`, the sky and all interreflection. The primary hit is the pixel-centre ray, as
in the path tracer.

- **World levels.** Level i has probes every `h0 2^i` metres over the scene's bounds, `6 (c0 2^i)^2`
  cube-map directions (a texel splits into 2 x 2 children, solid angles exact) and the interval
  `[T_i, T_i+1]`, `T_i = r0 h0 (f^i - 1) / (f - 1)`; the top level runs to infinity. Levels merge
  top-down: an entry traces its interval and, on a miss, takes its four child texels at the next
  level's 8 trilinear neighbours.
- **Bounces.** A ray that hits a surface returns `emission + albedo / pi x (sun + cache)`, the cache
  being level 0's merged radiance as an L1 irradiance per probe from the previous frame. Each frame
  adds one bounce; 7 frames give the path tracer's 8.
- **Screen probes (cascade 0 at lower density).** On the visible surface every `stride` pixels,
  `m^2` cosine-weighted rays (jittered strata) over `[0, T_1]`, continued from level 1. Pixels take
  the probes' irradiance by bilinear weights times a plane-distance and a normal test. Stride 0 skips
  them and gathers every pixel from the cache.
- **Cache gather.** Level 0's 8 probes, keeping those outside geometry, on the surface's lit side
  and, by Chebyshev's bound on 2 x 2 rays a texel of depth moments, able to see the point.

### The fixes

- `none`: plain trilinear merge.
- `probe` (the cheaper fix): one visibility segment a probe pair, computed once a probe, not once a
  ray; neighbours that cannot be seen or that sit inside geometry are dropped. Intervals also
  overlap the level above by `sqrt(3)` times its spacing, so a surface in the gap between an
  interval's end and a neighbour's interval start is met.
- `full`: the flatland path's fix, one ray from an entry's interval start to each neighbour's
  interval start (8 rays an entry).

## Running it

```sh
node --test web/shaders/cascades3d/cascades3d.test.mjs
python tests/web/cascades3d.py parity --adapter swiftshader     # CI
raw_native_cli gi-reference --owned-scene raw-hall-nave --width 512 --height 320 --export build/gi/hall.bin
raw_native_cli gi-reference --scene-file build/gi/hall.bin --pt build/gi/pt --width 1920 --height 1080 --spp 1024
python tests/web/cascades3d.py measure --adapter gpu --file build/gi/hall.bin --width 1920 --height 1080 \
  --plan '{"h0":0.5,"fix":"probe","stride":8,"m":6}' --static 1 --baseline 16 --save build/gi/run
python tests/web/cascades3d_metrics.py build/gi/run --pt build/gi/pt --images build/gi/img/hall
```

## Results on the RTX 4090

The error is pt-error's: relative RMSE of luminance over pixels both cover, with a 95% interval from
32 x 32 tile bootstrap. GPU time is the median and the 5th to 95th percentile of 50 frames from
timestamps, around the GI passes only. The GPU was shared with other training work during every
run. Evidence: `evidence/cascades3d-rtx4090.json`, bounds in `evidence/cascades3d-bounds.json`,
written before the first run.

Shipped setting: `h0 0.5 m, c0 1, r0 2, f 2, fix probe, stride 8, m 6`, probe flags and depth
moments computed once (static geometry).

| raw-hall nave | Relative RMSE | No-GI baseline | Reference noise | GI ms |
|---|---|---|---|---|
| 512 x 320 | 0.125 (0.107 to 0.151) | 0.395 | 0.014 | 2.6 |
| 1920 x 1080, run 1 | 0.104 (0.097 to 0.111) | 0.415 | 0.029 | 3.70 (3.49 to 4.12) |
| 1920 x 1080, run 2 | same frame | | | 4.04 (3.53 to 4.40) |
| 1920 x 1080, run 3 | same frame | | | 3.60 (3.46 to 4.20) |
| 1920 x 1080, flags every frame | same frame | | | 5.26 (4.92 to 6.05) |
| 3840 x 2160 | 0.111 (0.107 to 0.115) | 0.417 | not measured | 7.61 (7.16 to 9.12) |

Both P6 targets are met at 1080p: 0.104 against the 0.30 target (lighting model 2 reads 0.43; this
pipeline's no-GI baseline reads 0.40 to 0.42, which agrees), and 3.6 to 4.0 ms against 6 ms. Run to
run, the median moved by 12%. The primary hits take a further 1.5 ms at 1080p; the depth moments
take 7 to 8 ms once.

Cost cuts at 1080p (all `fix probe`): stride 4 with 64 rays reads 0.097 at 11.8 ms; stride 8 with 64
rays 0.102 at 4.7 ms; stride 8 with 36 rays 0.104 at 4.0 ms; world probes only (stride 0) 0.222 at
2.3 ms. Cascade 0 at an eighth of the linear density keeps the error within 7% of stride 4.
`c0 2` lowers the error to 0.088 at 9.1 ms; `f 4` raises it to 0.120.

**Planted check (white furnace).** A closed box, albedo 128/255 and emission 1 everywhere, with the
sky set to the fixed point `1 / (1 - rho)` and the cache started there: every correct estimator
returns that value at every pixel. The CPU reads it to 2e-16 and the GPU to 6e-8, in every fix mode.
The path tracer on the same box, against its own closed form `sum_{k<=8} rho^k`, reads 0.0043 (bias
1e-4, its sampling noise). The metric on the closed form plus 1% reads 0.0100.

**GPU parity.** Six cases (`evidence/shaders-cascades3d-parity-bounds.json`) against the float64
reference: worst 4.0e-5 on the RTX 4090 (bound 2e-3).

### Thin-wall leak

Two rooms behind a 4 cm partition, the sun through a skylight in room A, at 960 x 540 against 4096
spp references.

| Setting | Room A view | Room B view, room B pixels | Closed door: room B over room A |
|---|---|---|---|
| no GI | 0.595 | 1.66 (0.09 of the reference's light) | 0 |
| fix none | 0.503 | 90.6 (90x the light) | 0.87 |
| fix probe | 0.182 | 1.30 (0.53x the light) | 0.0009 |
| fix full | 0.277 | 46.7 (46x the light) | 0.44 |

Room B is dark (its reference is 2% of room A's), so its relative errors are large and its
reference noise is 0.24. The probe fix passes the kill criterion (room B error below the no-GI
baseline's) and the closed-room bound (under 1%). `none` and `full` fail both. The flatland path's
full fix does not carry over to 3D: a neighbour's child texels start their intervals at points that
straddle the thin wall, so light from room A comes in through directions the fix ray never checks.

### Glass (H150, flatland path)

The 3D path tracer has no dielectric, so H150's remedy was tested in `web/shaders/cascades`.
Inside 8 px of the glass circle, the fluence blends `w x traced + (1 - w) x cascades`, `w = 1 - d / 8`:
256 even directions follow the bent path exactly for S px, then read cascade 1 at the virtual probe
`end - dir_end x t_1`. At 3840 x 2160 (`evidence/cascades-h150-rtx4090.json`):

| Plan | S | Error 0 to 2 px | Error 2 to 8 px | Frame ms |
|---|---|---|---|---|
| b 2, 4, 4 | none | 0.808 | 0.750 | 5.2 |
| b 2, 4, 4 | 32 | 0.645 (-20%) | 0.690 | 9.2 |
| b 2, 4, 4 | 128 | 0.533 (-34%) | 0.623 | 13.9 |
| b 2, 4, 4 | 512 | 0.338 (-58%) | 0.512 | 20.1 |
| b 4, 4, 2 | none | 0.533 | 0.545 | 30.8 |
| b 4, 4, 2 | 32 | 0.276 (-48%) | 0.424 | 49 |
| b 4, 4, 2 | 128 | 0.299 (-44%) | 0.421 | 50 |

The kill condition (under 10% less) is not met. The 30% bound is met at S 32 on branch 4 and at S
128 or more on branch 2. The cost is about 12,000 band pixels times 256 rays at 4K. The branch-4
frame times move by 40% between runs, so its added cost is not resolved. In the render the dark
ring at the rim turns into a continuous rim; the banding further in, outside the band, stays.

## What the renders show

Renders, references and error maps at 3840 x 2160, opened at full size and in crops:

- raw-hall: light levels match the reference across the nave and both arcades. Walls and columns
  show a low-frequency mottle from the stride-8 probes' jittered rays, and column flutes are softened.
  The upper aisle wall beside the right gallery is darker than the reference over a patch about a
  metre across; that patch carries most of the error map's bright area.
- glass: see above.

## Limits

- Diffuse only, Lambert materials, one sun and a constant sky, as the path tracer's v2 interface.
- Static geometry for the shipped timing: depth moments (7 to 8 ms) are traced once. Probe flags
  every frame add 1.6 ms.
- Converged state after 7 frames of a static scene and light; a moving light needs those frames.
- The probe denoise below removes the fine probe mottle but not the larger blotches, which come
  from the world levels.
- The 4K reference has one seed, so its noise is not measured.

## Probe denoise (2026-11-07)

`denoise3d.mjs` (CPU, float64) and `wgsl3d-denoise.mjs` (GPU) add three switches to the plan. All are
off by default.

- `tf`, `tmax`: temporal accumulation. Every frame the screen probes move inside their stride cell
  (Halton 2, 3) and their rays take a new seed. Each pixel's irradiance is blended with its history,
  reprojected through the previous camera, bilinear over 4 pixels. A tap counts only when its
  surface lies within 4 pixel footprints of the pixel's plane and its normal is within dot 0.9. The
  blend weight is 1 / n, with n at most `tmax`. History starts after the 7 warm-up frames.
- `sf`, `sl`: `sf` a-trous passes (steps 1, 2, 4) of a 3 x 3 kernel over the probe grid, with the
  upsampling's plane and normal test, and with `sl` > 0 a luminance edge stop.
- `move: { d, k }`: the camera starts `d` away and slides onto the scene camera over the last `k`
  frames, to test reprojection.

Bounds were written before the first run: `evidence/cascades3d-denoise-bounds.json`. Results are in
`evidence/cascades3d-denoise-rtx4090.json`. "Mottle" is the RMS of the relative error band-passed
between Gaussian sigma 4 px and 32 px on wall and column pixels. A planted 5% sinusoid reads 0.0314
(expected 0.0354), and the second reference seed against the first reads 0.0032.

raw-hall nave at 1920 x 1080 against the 8-bounce path trace, with the round-1 setting:

| Setting | Relative RMSE | Wall mottle | Floor mottle | GI ms, three runs |
|---|---|---|---|---|
| none (round 1) | 0.104 (0.097 to 0.111) | 0.040 (0.037 to 0.045) | 0.030 | 4.04, 3.59, 3.71 |
| temporal, 32 frames | 0.095 (0.088 to 0.101) | 0.036 (0.032 to 0.041) | 0.026 | 4.30, 4.31, 4.12 |
| a-trous 2, sl 0.5 | 0.099 (0.093 to 0.106) | 0.034 (0.030 to 0.037) | 0.022 | 3.95 |
| both | 0.094 (0.088 to 0.101) | 0.033 (0.029 to 0.036) | 0.022 | 4.29, 4.36, 4.33 |
| both, moving camera | 0.095 (0.089 to 0.102) | 0.033 | 0.022 | |
| none, 4K | 0.111 (0.107 to 0.115) | 0.034 | 0.028 | 7.4 |
| both, 4K | 0.105 (0.101 to 0.109) | 0.028 | 0.021 | 8.6 |

The temporal filter lowers the error by 9% and the bias from -0.5% to -0.1%. Most of that gain
comes in the first 8 frames: 0.104, 0.099, 0.096, 0.095 and 0.095 at 1, 2, 4, 8 and 16 frames of
history. A planted step of probe irradiance (0.2 to 1.0 across a floor line) widens from 7.4 px
(10% to 90%) to 9.9 px with the temporal filter, 8.1 px with a-trous and the luminance stop, and
11.3 px with both. Without the luminance stop a-trous widens it to 37 px. With the stop, a 2% to 5%
tail still reaches 20 px past the edge.

The pre-registered targets were missed. The best band mottle fell 19%, against a 50% target and a
25% kill line. RMSE reached 0.094 against a 0.090 target. The denoise therefore stays off by
default. The renders explain the miss. The fine speckle from the stride-8 probes is gone in the
temporal frames, and a probe-scale band (sigma 1 to 8 px, added after the first runs) falls from
0.025 to 0.017, with a 0.012 reference-noise floor. The blotches that remain are 50 to 150 px across.
Neither filter changes them, so they come from the world levels and the cache, not from probe
noise. Longer screen-probe intervals (`ov` 2 or 3, `r0` 4) made those blotches worse: RMSE rose to
0.12, with a 5% bias.
