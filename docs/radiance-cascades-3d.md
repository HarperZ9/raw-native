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
# the round-1 setting, without the later rounds' defaults:
#   --plan '{"h0":0.5,"fix":"probe","stride":8,"m":6,"sv":0,"ft":0,"cg":0,"tf":0,"pi":0}'
# the sixth round's default (J2): add "pi":0,"cg":2,"sk":0 to the first plan
```

Since 2026-11-07 (sixth round, below) the plan's defaults are the leak-tight merges `sv 2`, `ft 1`,
`cg 2` and world-grid jitter with temporal accumulation, `jit 8` and `tf 32`. Since the seventh round
the defaults also test probes for being buried in geometry (`pi 1`), fall back to traced segments
where the depth tests find no probe (`cg 6`), and skip the entries of buried probes (`sk 1`). The first
command above runs that setting. A frame then shows the converged result after 7 warm-up frames and 32 frames of
history; the scene and camera must hold still for that long, or the history is reprojected
(`move`).

## Results on the RTX 4090

The error is pt-error's: relative RMSE of luminance over pixels both cover, with a 95% interval from
32 x 32 tile bootstrap. GPU time is the median and the 5th to 95th percentile of 50 frames from
timestamps, around the GI passes only. The GPU was shared with other training work during every
run. Evidence: `evidence/cascades3d-rtx4090.json`, bounds in `evidence/cascades3d-bounds.json`,
written before the first run.

Round-1 setting (the shipped setting until the sixth round): `h0 0.5 m, c0 1, r0 2, f 2, fix probe,
stride 8, m 6`, probe flags and depth moments computed once (static geometry). The table below is
that setting; the current default adds `sv 2, ft 1, cg 2, jit 8, tf 32` and reads 0.082 at 5.0 ms
(see "Cheaper visibility and a new default").

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
- The round-1 setting's thin-wall result holds only at the measured grid placement (see "World-level
  options"). The current default (seventh round) reads 0.015% to 0.048% of room A's light in the
  closed room at every placement tested and at most 0.15% at the fixed phase grids.
- The default needs 39 frames of a still scene to converge (7 bounces, 32 frames of history), and
  the first frame of each of its 8 grid phases traces that phase's depth moments and start
  visibility (static geometry). At 4K it takes 10.6 ms.
- Since the seventh round the CPU white furnace holds to 1e-6 at all 21 grids tested with the
  default switches. A surface that no probe is shown to see, by depth or by a traced segment, still
  gets no light; the furnace no longer meets one.
- raw-hall reads about 1.4% bright, and a bright band on the right-hand column flanks and the arch
  soffits remains (seventh round, below). The door-lit room B gets 0.69 of its light (J2 0.83).
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

## Leak-tight merges (2026-11-07, fifth round)

Three opt-in switches, CPU (`cascades3d.mjs`, `world.mjs`) and GPU (`wgsl3d-passes.mjs`,
`wgsl3d-core.mjs`, `wgsl3d-world.mjs`), with four new parity cases. Bounds and the kill line were
written before any run: `evidence/cascades3d-leak-bounds.json` (one amendment, logged before its run).
Results: `evidence/cascades3d-leak-rtx4090.json`.

- `sv: 2`: start visibility of the entry's own ray. An entry's value comes from one ray, the texel
  centre's. `sv 1` weighed it by the share of 4 sub-texel rays that cross `[0, T_i]` unblocked, so an
  entry whose own ray had passed through the wall still counted at 1/4 to 3/4. `sv 2` traces the
  centre ray over `[0, T_i]` once (static geometry) and weighs the entry 1 or 0.
- `ft: 1`: a merge that trusts no neighbour traces its own ray on from its interval's end to the
  end of the scene (a hit returns the surface with the cache; a miss the sky). The old default took
  the plain mean of all eight neighbours, which leaks; `fz` took no light, which goes dark.
- `cg: 1`: when no level-0 probe passes the cache gather's depth test, only lit-side probes that an
  unblocked segment from the surface reaches count; none gives no light. The old chain fell back to
  probes without a visibility test.

Each switch keeps a chain of unblocked segments from the pixel to every light it gathers, so in a
closed room nothing from the other side can arrive. That argument is inferred; the runs below test it.

Closed room at 960 x 540, room B over the room A view's light (bound 1%):

| Setting | g0 | g1 | g2 | g3 | g4 | Phases 4 to 7 (fixed grids) |
|---|---|---|---|---|---|---|
| B0 (round 4) | 0.10% | 14.3% | 37.0% | 9.9% | 10.4% | |
| jit 8, tf 32 (round 4) | 17.9% | 16.7% | 14.7% | 14.1% | 15.3% | |
| L: B0 + sv 2, ft, cg | 0.009% | 0.031% | 0.002% | 0.006% | 0.000% | 0.003%, 0.006%, 0.002%, 0.15% |
| L without cg | 0.014% | 0.035% | 0.074% | 0.023% | 0.020% | |
| L with sv 1 | 0.009% | 0.030% | 4.9% | 0.005% | 0.020% | |
| J: jit 8, tf 32 + sv 2, ft, cg | 0.15% | 0.30% | 0.40% | 0.17% | 0.12% | |
| J without cg | 0.45% | 0.71% | 0.53% | 0.45% | 0.41% | |
| J with sv 1 | 2.2% | 1.5% | 1.6% | 0.87% | 0.75% | |

Phases 0 to 3 of jit 8 sit at g1 to g4. On the CPU at 16 x 10 px, `sv 2` with `ft` reads 0.000% at
all 12 grids; `sv 1` with `ft` still reads 0.97% at g2, which is how the sub-texel share was found to
be the leak. Door-lit room B, light ratio and error (no GI 1.66): B0 0.53, 1.33; L 0.51, 1.27; J 0.83,
0.94. J is the first setting that lights room B to within a fifth of the reference without leaking.

raw-hall nave at 1920 x 1080 against the 8-bounce path trace:

| Setting | Relative RMSE | Bias | Wall mottle | GI ms |
|---|---|---|---|---|
| B0 | 0.104 (0.097 to 0.111) | -0.5% | 0.040 | 3.82 |
| sv 2 | 0.113 (0.105 to 0.120) | +2.1% | 0.042 | 4.06 |
| L without cg | 0.114 (0.107 to 0.122) | +2.6% | 0.043 | 4.68 |
| L | 0.112 (0.105 to 0.120) | +2.4% | 0.043 | 4.94 |
| jit 8, tf 32 | 0.080 (0.075 to 0.085) | +0.1% | 0.030 | 4.92, 4.66 |
| J without cg | 0.086 (0.081 to 0.092) | +2.0% | 0.032 | 5.02, 4.97 |
| J | 0.083 (0.078 to 0.088) | +1.4% | 0.032 | 6.66, 6.38, 6.58 |
| 4K: L / J | 0.118 / 0.094 | +2.3% / +1.4% | 0.036 / 0.025 | 10.5 / 12.3 |

Decisions under the pre-registered rules: no default changes.

- L closes the shipped default's leak (worst 0.15% over 9 grids) but misses the error bound by 0.001
  (0.112 against B0's upper limit 0.111). `sv 2` alone accounts for it: dropping the entries whose
  skipped start crossed a surface also drops the dark values those entries held, and the hall comes
  out 2% bright.
- J meets the leak and error bounds and misses time: 6.4 to 6.7 ms against 6. `cg` runs a segment
  test at every traced hit whose depth test finds no probe, about 1.2 ms in the level pass. Without
  `cg`, J fits (5.0 ms) and stays under 1% (0.71% worst) but misses the error bound (0.086 against
  0.085).
- The kill line (worst leak above 1.3%, or a loss of light over 10%) is not met: the approach works,
  and the remaining gaps are cost and a 2% bias.

Renders (`H:` renders folder `cascades3d-leak-2026-11-07`), opened at full size: the closed room under
L is black with a few isolated speckles; under J a faint glow remains along the wall-ceiling and
wall-wall seams, which is where its 0.1% to 0.4% sits. In the door-lit room B0 and L show bright
probe speckles; J is smooth. In raw-hall the error crops show L brighter than B0 along column edges
facing the aisle wall, and J matching jit except one brighter band on a column flank.

## Cheaper visibility and a new default (2026-11-07, sixth round)

Two opt-in switches, CPU (`cascades3d.mjs`, `world.mjs`) and GPU (`wgsl3d-core.mjs`,
`wgsl3d-passes.mjs`, `wgsl3d-world.mjs`), with five new parity cases. Bounds and the kill line were
written before any run: `evidence/cascades3d-cost-bounds.json` (two amendments, each logged before
the runs it governs). Results: `evidence/cascades3d-cost-rtx4090.json`.

- `cg: 2` (and `3`): the cache gather's fallback without rays. The depth pass already traces 4 rays
  a texel from every level-0 probe, once a grid phase; it now also keeps each texel's nearest and
  farthest distance. When no probe passes the depth-moment test, a lit-side probe outside geometry
  counts if the farthest (`cg 2`) or nearest (`cg 3`) of those rays toward the point reaches past
  it. `cg 1` traced a segment to each of up to 8 probes at every such hit.
- `sd: 1` (with `sv 2`): an entry whose own ray is blocked over the skipped `[0, T_i]` traces from 0
  instead and holds the surface that blocks it, so no entry is dropped. A first version that capped
  the dropped entries at the trusted mean failed the white furnace on the CPU (1.4%) and was
  replaced before any GPU run.

raw-hall nave at 1920 x 1080 against the 8-bounce path trace (GPU shared with training and another
agent, 17.7 to 18.7 GB of 24.5 GB in use by them):

| Setting | Relative RMSE | Bias | Wall mottle | GI ms (p5 to p95) |
|---|---|---|---|---|
| B0, round-1 setting | 0.104 (0.097 to 0.111) | -0.5% | 0.040 | 3.93 (3.88 to 4.16) |
| J: jit 8, tf 32, sv 2, ft 1, cg 1 (round 5) | 0.083 (0.078 to 0.088) | +1.4% | 0.032 | 6.30 (5.85 to 7.19) |
| **J2: J with cg 2 (the new default)** | **0.082 (0.077 to 0.088)** | +1.3% | 0.032 | **5.05, 5.02, 5.01** (4.88 to 5.37) |
| J with sd | 0.130 (0.121 to 0.139) | -6.9% | 0.035 | 6.16 |
| K: J2 with sd | 0.130 (0.121 to 0.139) | -7.0% | 0.035 | 4.97 |
| J2 at 3840 x 2160 | 0.093 (0.090 to 0.097) | +1.3% | 0.025 | 10.2 (10.0 to 10.6) |

Closed room at 960 x 540, room B over the room A view's light (bound 1%):

| Setting | g0 | g1 | g2 | g3 | g4 | ph4 to ph7 (fixed grids) |
|---|---|---|---|---|---|---|
| J2 | 0.15% | 0.29% | 0.39% | 0.16% | 0.12% | |
| J2 fixed grid (no jit) | 0.009% | 0.031% | 0.002% | 0.006% | 0.000% | 0.003%, 0.006%, 0.002%, 0.15% |
| K | 0.17% | 0.26% | 0.40% | 0.16% | 0.13% | |
| K fixed grid | 0.009% | 0.019% | 0.001% | 0.007% | 0.000% | 0.003%, 0.004%, 0.001%, 0.24% |

Door-lit room B, light ratio and error (no GI 1.66): J2 0.83, 0.94; K 0.63, 1.02. On the CPU at
16 x 10 px every combination of `sv 2`, `ft`, `cg 2`, `cg 3` and `sd` reads 0.000% at 13 grids.

What this shows:

- `cg 2` does what `cg 1` did at about the cost of no `cg` (round 5: 5.0 ms): 1.25 to 1.29 ms less GI time in the same
  session, the same leak to within 0.01 percentage point and the same error. The fallback rarely
  fires, so the cost of `cg 1` was its rays, not its effect.
- `sd` is a null. The surfaces that block an entry carry less light than the merge needs, and the
  hall comes out 7% dark instead of 1.4% bright. The renders show the error rising over columns,
  floor and far wall alike. `sv 2`'s brightening stays open.
- J2 meets every bound: leak under 0.4% at 5 placements and at most 0.15% at all 8 phase grids, RMSE
  0.082 (target 0.080 to 0.085; bound 0.111), and three timing medians of 5.0 ms against 6. It is the
  new default of `planCascades3d`. Parity: all 24 cases within 2e-3 on the RTX 4090; worst 1.4e-4,
  and the 19 earlier cases read exactly as before.

Renders (`H:` renders folder `cascades3d-cost-2026-11-07`: the 4K frame, reference and error map,
4K crops of B0, J and J2, 1:1 zooms), opened at full size: J and J2 are indistinguishable at 1:1;
against B0 the 100 px blotches on the right aisle wall are gone, and a bright band on one column
flank and thin lines under the arches remain. Under K the whole error map lifts.

## Buried probes and the dark fallback (2026-11-07, seventh round)

Four opt-in switches, CPU (`cascades3d.mjs`, `world.mjs`) and GPU (`wgsl3d-core.mjs`,
`wgsl3d-passes.mjs`, `wgsl3d-world.mjs`, `gpu3d.mjs`), with seven new parity cases. Bounds and the kill
lines were written before any leak, GPU or raw-hall run: `evidence/cascades3d-furnace-bounds.json` (four
amendments, each logged before the runs it governs). Results: `evidence/cascades3d-furnace-rtx4090.json`.
Source: J. Tollenaar's article "Split Radiance Cascades", which names probes buried in geometry as a
cause of leaks and dark splotches and traces from real surface points instead. Its sparse hashed
probes and its transmittance-weighted merge were not built.

What made the white furnace read low. Round 6 saw 1.05% at one grid. A sweep over 21 grids (17
distinct) and three plans found J2 low at 8, 12 and 20 of them, by up to 3.6%, 15.7% and 2.1%. Two
causes, each found by returning the exact value for one class of gather at a time:

- Probes inside the 0.2 m wall slabs were not flagged as inside. The test cast 6 axis rays one
  spacing long and needed 3 back-face hits; in a thin slab only 2 axis rays meet a face. Their own
  rays met the slab from inside, gathered nothing there, and room surfaces took their dark caches
  through the depth test.
- A probe 1 cm from a wall sees the floor straight below it, but the depth texel that holds that
  direction also spans the wall, so its farthest ray stops at the wall and `cg 2` drops the probe.

The switches:

- `pi: 1`: a probe also counts as inside when at least half of 96 rays (the texel centres of face
  side 4) meet a surface from behind. Traced with the flags, once a grid phase. "More than half"
  failed first: a probe 3.5 cm above the floor inside a block that rests on it reads exactly half,
  because the block's base and the floor are coplanar.
- `cg: 4`, `5`, `6`: after `cg 2`'s depth test, a traced segment from the surface to the probes.
  `cg 4` tests all eligible probes (as `cg 1`), `cg 5` only the one of largest weight, `cg 6` goes
  through them in decreasing weight and stops at the first it reaches.
- `sk: 1`: the level pass skips the entries of probes flagged inside. With `fix probe`, `ft` and
  `cg`, no merge or gather reads them, so frames are byte-identical with and without it.
- `fd: 1` (screen probes) or `2` (every merge): a merge that drops part of its trusted weight to
  start visibility fills that part with its own ray traced on, instead of renormalising.

CPU white furnace at 12 x 8 px, 21 grids, worst relative error (plans: stride 3; stride 0; stride 3
with `jit 2`):

| Setting | Stride 3 | Stride 0 | Jit |
|---|---|---|---|
| J2 | 3.6%, 8 grids fail | 15.7%, 12 fail | 2.1%, 20 fail |
| `pi`, `cg 4`, first version (more than half) | 2e-16 | 2e-16 | 0.074%, 2 fail |
| `pi`, `cg 4` (F) | 2e-16 | 2e-16 | 2e-16 |
| `pi`, `cg 5` (F5) | 0.0003%, 1 fails | 4e-7 | 1.16%, 2 fail |
| `pi`, `cg 6`, `sk` (H) | 2e-16 | 2e-16 | 2e-16 |

Every candidate reads 0.000% in the CPU closed room at 13 grids.

raw-hall nave at 1920 x 1080 against the 8-bounce path trace (GPU shared with a training job, 12.6
to 14.6 GB in use by it):

| Setting | Relative RMSE | Bias | GI ms, three runs |
|---|---|---|---|
| J2 control (the sixth round's default) | 0.082 (0.077 to 0.088) | +1.33% | 4.93, 4.93, 5.00 |
| J2 with `pi` | 0.082 | +1.36% | 5.00 |
| F: J2 with `pi`, `cg 4` | 0.083 (0.078 to 0.088) | +1.46% | 6.20, 6.59, 6.39 |
| F with `sk` | 0.083 | +1.46% | 5.27 |
| G: F with `fd 1` | 0.082 (0.077 to 0.087) | +1.36% | 7.52, 7.43, 7.58 |
| F with `fd 2` | 0.082 | +1.51% | 8.18 |
| **H: J2 with `pi`, `cg 6`, `sk` (the new default)** | **0.083 (0.078 to 0.088)** | +1.45% | **5.19, 5.17, 5.18** |
| H at 3840 x 2160 | 0.094 (0.091 to 0.097) | +1.40% | 10.6 |

Closed room at 960 x 540, room B over the room A view's light (bound 1%): H 0.015%, 0.034%, 0.032%,
0.048%, 0.043% at g0 to g4, where J2 read 0.12% to 0.39%; H on fixed grids at most 0.15% (ph7) over 9
grids. Door-lit room B: light ratio 0.69, error 0.97 (J2 0.83, 0.94; no GI 1.66).

What this shows:

- H meets every bound and is the new default of `planCascades3d`: `pi 1`, `cg 6`, and `sk 1` wherever
  it is exact. Pass `pi: 0, cg: 2, sk: 0` for J2. Parity: 31 cases within 2e-3, worst 1.4e-4; the
  earlier cases pin `pi` and `sk` to 0 and read as before.
- `pi` cuts the closed-room leak eightfold: the residual glow along J2's wall seams came from probes
  buried in the 4 cm wall. It costs nothing a frame.
- F's 1.3 ms came almost all from entries of buried probes, whose rays meet geometry from inside and
  fall through to segments; nothing reads those entries. `sk` removes them without changing a pixel.
- Nulls: `fd` leaves the bias where it was, so sv 2's brightening does not come from renormalising
  over kept texels; `cg 5` (one segment) does not hold the furnace.
- The bright band (column flanks, the far-wall patch, arch soffits; a region of 23,000 px chosen on
  round 6's frame) did not move under any candidate: mean signed error +0.195 to +0.205 against the
  control's +0.203. On earlier rounds' frames the same region reads +0.106 for the round-1 setting,
  +0.113 with temporal accumulation alone, +0.159 with `sv 2` and +0.170 with `jit`: start visibility
  and grid jitter each add about half of it.

Renders (`H:` renders folder `cascades3d-furnace-2026-11-07`: the 4K frame, reference and error map,
crops of B0, J2 and H, 1:1 error zooms of J2 against H, the closed room at 40x exposure), opened
myself: in raw-hall J2 and H look the same at 1:1, and the column-flank band and the bright lines
under the arches remain in both. In the closed room at 40x, J2's walls glow evenly and its seams
shine; under H the walls are dark, and a fainter line along the far wall's seams and sparse
speckles remain.
