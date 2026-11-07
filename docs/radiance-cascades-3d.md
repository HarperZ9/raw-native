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
# world-grid jitter with temporal accumulation (opt-in; it leaks through thin walls, see below)
#   --plan '{"h0":0.5,"fix":"probe","stride":8,"m":6,"tf":32,"jit":8}'
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
baseline's) and the closed-room bound (under 1%) at the grid placement measured here. Correction
(2026-11-07, fourth round): that closed-room pass depends on where the grid sits against the wall.
With the grid moved to four other placements the probe fix reads 9.9% to 37% (see "World-level
options" below). `none` and `full` fail both. The flatland path's
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
  from the world levels. Grid jitter (`jit`) removes most of them but leaks through thin walls.
- The thin-wall result holds only at the measured grid placement (see "World-level options").
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

## World-level options (2026-11-07, fourth round)

The blotches the denoise left (50 to 150 px across) come from the world levels. Four plan switches
went after them, each off by default, written in `world.mjs` (CPU) and `wgsl3d-world.mjs` with the
merges in `wgsl3d-passes.mjs` (GPU), with parity cases. Bounds were written before any GPU run:
`evidence/cascades3d-world-bounds.json`. Results: `evidence/cascades3d-world-rtx4090.json`.

- `c1`: level 1's face side (4 gives 96 directions instead of 24). Level 2 keeps at least `c1`, so a
  parent texel splits into 1, 4 or 16 children.
- `mw: "vis"`: a screen probe weighs level 1's neighbours by the depth-moment (Chebyshev) visibility
  of its surface point from the level-0 probe at the same place, instead of the on/off segment test.
- `sv: 1`: start visibility. Level i's entry for direction d traces from `q + d T_i`, so it skips the
  first `T_i` of its ray. A probe 0.2 m in front of a wall therefore holds, for directions toward
  the wall, the light behind it. With `sv` each entry carries the share of 4 sub-texel rays that cross
  `[0, T_i]` unblocked (traced once for static geometry), and every merge weighs a neighbour's texels
  by it. `fz: 1` makes a merge that trusts no neighbour return no light instead of the plain mean
  of all eight.
- `jit: K` (with `tf`): the whole world grid moves by one of K offsets a frame (Halton 3, 5, 2 times
  the level-1 spacing), and the temporal accumulation averages the K results. Depth moments, flags
  and start visibility belong to a phase and are traced on its first frame, then reused (static
  geometry). `go: [x, y, z]` moves the grid by a fixed offset, for placement tests.

raw-hall nave at 1920 x 1080 against the 8-bounce path trace, the round-1 setting plus the switch:

| Setting | Relative RMSE | Bias | Wall mottle | Floor mottle | GI ms (p5 to p95) |
|---|---|---|---|---|---|
| B0, round-1 setting | 0.104 (0.097 to 0.111) | -0.5% | 0.040 (0.037 to 0.045) | 0.030 | 3.87 (3.56 to 4.22) |
| c1 4 | 0.100 (0.094 to 0.107) | +1.3% | 0.039 (0.036 to 0.042) | 0.028 | 5.64 (4.96 to 5.96) |
| mw vis | 0.103 (0.097 to 0.110) | -0.5% | 0.040 (0.036 to 0.044) | 0.030 | 3.82 (3.32 to 3.98) |
| sv | 0.107 (0.100 to 0.114) | +1.6% | 0.040 (0.036 to 0.045) | 0.033 | 4.01 (3.57 to 4.23) |
| sv, fz | 0.269 (0.251 to 0.289) | -20.9% | 0.053 | 0.100 | 3.97 |
| T: temporal 32 | 0.095 (0.088 to 0.101) | -0.1% | 0.036 (0.032 to 0.041) | 0.026 | 4.51 (3.94 to 4.98) |
| DN: temporal 32, a-trous 2 | 0.094 (0.088 to 0.101) | -0.1% | 0.033 (0.029 to 0.037) | 0.022 | 4.53 (4.07 to 5.02) |
| jit 8, temporal 32 | **0.080 (0.075 to 0.085)** | +0.1% | **0.030 (0.028 to 0.033)** | **0.018** | 4.76 (4.14 to 5.03) |
| jit 8, temporal 32, sv | 0.083 (0.078 to 0.088) | +1.2% | 0.031 (0.028 to 0.034) | 0.018 | 4.57 (4.08 to 5.00) |
| jit 8, temporal 32, sv, fz | 0.237 (0.221 to 0.255) | -18.4% | 0.044 | 0.096 | 4.59 |

At 3840 x 2160 (one reference seed): B0 0.111 (0.107 to 0.115), wall mottle 0.034, 8.1 ms; temporal
32 0.105 (0.101 to 0.109), 0.029, 9.3 ms; jit 8 with temporal 32 0.091 (0.087 to 0.094), 0.024, 8.8 ms.

Renders (`H:` renders folder `cascades3d-world-2026-11-07`, 4K frames, error maps and crops of the
same regions for B0, temporal and jit), opened at full size and in crops: on the right aisle wall
B0's error map is a field of 100 px blotches over a bright patch beside the window; temporal
accumulation removes the speckle but keeps the patch; with jit the patch and the blotches are gone,
and what is left is a smooth, low gradient and thin bright lines under the arches. On the left
columns the flute speckle and the wall blotches vanish with jit; a broad error band on the far wall
stays.

Thin-wall leak at 960 x 540, the closed room's room B over the room A view's reference mean (bound
1%), at the shipped grid placement (g0) and four others (`go` in the evidence):

| Setting | g0 | g1 | g2 | g3 | g4 | Door-lit room B: light, error (no GI 1.66) |
|---|---|---|---|---|---|---|
| B0 | 0.10% | 14.3% | 37.0% | 9.9% | 10.4% | 0.53, 1.33 |
| sv | 0.12% | 0.22% | 4.8% | 1.1% | 4.3% | 0.64, 1.35 |
| sv, fz | 0.01% | 0.04% | 0.04% | 0.58% | 0.93% | 0.25, 1.53 |
| jit 8, temporal 32 | 17.9% | 16.7% | 14.7% | 14.1% | 15.3% | 18.8, 18.4 |
| jit 8, temporal 32, sv | 3.2% | 2.5% | 2.4% | 2.3% | 2.5% | 3.9, 3.2 |
| jit 8, temporal 32, sv, fz | 2.0% | 1.4% | 1.6% | 1.3% | 1.6% | 2.7, 2.0 |

What this shows:

- The probe fix is leak-tight only by luck of placement. At four other placements of the same grid
  the closed room gets 10% to 37% of room A's light. Any other scene is another placement, so the
  round-1 leak result does not carry. The cause is the skipped start of each interval, found on the
  CPU at 16 x 10 px before the GPU runs.
- `jit` is the one switch that cuts the blotches. Against temporal accumulation alone it lowers the
  error by 16% (0.095 to 0.080) and the wall mottle by 17% (0.036 to 0.030), and the floor mottle by
  33%; against the round-1 setting the wall mottle falls 26%, past the original 25% line. Its error
  map loses most of the blotches. But it fails the leak bound: every jitter phase is another grid
  placement, and the history averages the leaky ones in (14% to 18%).
- `sv` cuts the placement leak 4 to 300 times and lifts the door-lit room from 0.53 to 0.64 of its
  light, but two placements stay over 1% (4.8%, 4.3%). The rest comes from merges that reject all
  eight neighbours and then take all eight back. `fz` closes that (every placement under 1%) but
  darkens raw-hall by 21%, where many merges near columns trust no neighbour: a null.
- `c1` and `mw vis` are nulls for the blotch (-4% and -2% wall mottle). `c1` costs 1.8 ms. `mw vis`
  saves 0.28 ms (no segment rays) at no cost in error.
- Only 3% to 8% of the B0 wall mottle's energy is locked to the grid's cell phase (512 bins; a
  shuffled control reads 0.05%; `tests/web/cascades3d_gridphase.py`, exploratory). The blotches are
  parallax error that changes with the grid's offset, which is why averaging offsets works and
  sharper directions do not.

Decisions under the pre-registered rules: no switch becomes a default. `sv` fails the five-placement
leak check, `sv` with `fz` fails the error bound, and `jit` fails the leak bound, so the denoise also
stays off even though `jit` with temporal accumulation clears the 25% mottle line. Parity: all 15
cases (5 new) within 2e-3 on the RTX 4090; worst 1.4e-4.

Not closed: with `jit`, `sv` and `fz`, the CPU at 16 x 10 px shows one phase of eight leaking 2%
through the screen probes, not through the cache's fallback. A leak-tight `jit` needs that path, and
a merge fallback that neither leaks nor goes dark.
