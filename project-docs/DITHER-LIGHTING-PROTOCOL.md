<!-- writing-profile: research -->
# Dither-quantised lighting: motion comparison protocol

Written and committed on 2026-10-15 before any shimmer metric was run on any threshold or anchoring variant. The commit that holds this note also holds the metric code (`tools/shaders/ditherlight_metrics.mjs`) and the model prediction (`evidence/ditherlight-motion-prediction.json`, made by `tools/shaders/ditherlight_prior.mjs` from the motion field and tone only; no threshold was read). The measurement tool (`tools/shaders/ditherlight_motion.mjs`) does not exist yet at this commit.

## Question

The lighting model (web/shaders/ditherlight) resolves tone through a threshold matrix. Two choices are open (NOTES item 1, open questions):

1. Threshold source: Bayer 8x8 or the engine's 64x64 blue-noise mask.
2. Anchoring: to the screen, to the world (a fixed cell size in world units), or to the world with the cell doubled until it covers about 2 pixels (world-octave). The anchor position is in the object's own space, so a moving object carries its dots.

## Stimuli

The original scene "The Salt Road" (web/shaders/fixtures/dream*.mjs), 256 x 144, 1 bit (levels 2), preset `ink`, cell 0.1 m, octave target 2 px, fog distance 38 m, 32 frames per path. Five moving paths and one control:

| path | motion |
|---|---|
| dolly | camera forward 0.08 m per frame |
| strafe | camera sideways 0.06 m per frame |
| yaw | camera turns 0.7 degrees per frame |
| orbit | camera circles the tower at 14 m, 0.5 degrees per frame |
| spin | camera still, the tower turns 1.2 degrees per frame |
| static | nothing moves (control) |

Lights are fixed in the world on these paths (lantern, two orbs, lighthouse beam), so a surface point's tone changes only through fog. The demo render attaches the lantern to the camera; the measurement does not, because a real tone change would count as shimmer.

## Metrics (defined in ditherlight_metrics.mjs)

- L, surface lock: fraction of valid pixels whose level differs from the level at the same surface point one frame earlier (exact motion vector, nearest pixel).
- F, boil: 100 x mean |e_t - e_(t-1)| in screen coordinates, e = cbrt(box4(output)) - cbrt(box4(tone)), linear light.
- A, accuracy: 100 x mean |e_t|.
- S, seam: for world-octave, the fraction of valid pixels whose octave differs from the reprojected pixel's.
- Controls: static path (every L and F must be exactly 0); a displaced control that compares each pixel with the pixel 3 px to the right in the previous frame (L must rise to about the independent-threshold value).

Six cells: {bayer8, blue64} x {screen, world-fixed, world-octave}.

## Predictions, with bounds

The L model (predictL) uses: two independent thresholds give different levels with probability 2f(1-f); screen anchoring changes cell when round(motion) is nonzero; world anchoring changes cell with probability min(1, (|dx|+|dy|) / cell size in pixels), where dx, dy are the sub-pixel residual of the reprojection. Values from the first frame pair of each path, mean over valid pixels:

| path | independent 2f(1-f) | L screen | L world-fixed | L world-octave |
|---|---|---|---|---|
| dolly | 0.333 | 0.205 | 0.101 | 0.050 |
| strafe | 0.333 | 0.257 | 0.078 | 0.030 |
| yaw | 0.325 | 0.325 | 0.113 | 0.052 |
| orbit | 0.269 | 0.118 | 0.110 | 0.037 |
| spin | 0.294 | 0.000 | 0.001 | 0.000 |

- P1. Measured L (mean over the 31 frame pairs, either threshold source) is within the larger of 35% relative or 0.02 absolute of the model value on dolly, strafe, yaw and orbit; on spin it is at most 0.01 for every cell.
- P2. Ordering L(screen) > L(world-fixed) > L(world-octave) on dolly, strafe and yaw. On orbit no ordering between screen and world-fixed is claimed (the model puts them within 8%); world-octave is lowest.
- P3. F(world-fixed) is at least 1.3 x F(screen) on dolly, strafe and yaw, and F(world-octave) is at least F(screen). The model has no F; these are directional claims.
- P4. Bayer against blue noise, for each anchor: |L(bayer) - L(blue)| / L is at most 10%; F(bayer) is at most F(blue); A differs by at most 15%.
- P5. Static control: L = F = 0 exactly in all six cells. Displaced control, screen anchoring: L in [0.7, 1.1] x the independent 2f(1-f) value.
- P6. S (world-octave): on dolly between 0.2% and 3%; on strafe and yaw at most 1%.
- P7. Outcome: world-octave with Bayer8 wins the decision rule below.

## Decision rule

1. Per path (the five moving paths), Ln = L / max over the six cells, Fn = F / max over the six cells, score = max(Ln, Fn); a cell's score is the mean over paths. The lowest score wins the anchor, subject to the accuracy gate: mean A within 10% of the best cell's.
2. Threshold source: if, averaged over the three anchors, L and F differ by less than 10% relative between Bayer8 and blue64, the sources are not separated by this metric, and the choice is Bayer8 on cost (analytic, no table fetch, exact on CPU and GPU), recorded as a cost choice. If they differ by 10% or more, the source with the lower mean of Ln and Fn wins.
3. A second look: 12 stills (6 cells x 2 frames) are shown to the author of this note in random order with the key hashed beforehand (hash in the results). This is one rater who wrote the code, so it is not an independent blind test and it never overrides the metric; it can add a caveat.

## What would falsify the plan

If L does not track the model within P1 on the screen cell, the metric or the reprojection is wrong and no choice is made from it. If the static control is not exactly zero, the instrument is broken.

## Limits

Software metric on one scene and one resolution. Models a viewer who either tracks surfaces (L) or fixates (F); neither is a perception experiment. Human ratings remain the standard, and none were collected.
