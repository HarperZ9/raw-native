<!-- writing-profile: research -->
# Dither-quantised lighting: motion comparison results

Run 2026-10-15 under the protocol committed first (DITHER-LIGHTING-PROTOCOL.md, commit 8769a4e). Raw numbers: `evidence/ditherlight-motion-runs.json`. Post hoc diagnostics: `evidence/ditherlight-motion-exploratory.json` (decides nothing).

## Choice

Default for the `ink` and `tones4` presets: **Bayer 8x8 thresholds, screen-anchored**. World-fixed and world-octave anchoring stay available as options.

Why, by the preregistered rule:
- Threshold source. Over the five moving paths and three anchors, Bayer8 and blue64 differ by 0.5% in L and 2.4% in F, under the 10% line, so the metric does not separate them. Bayer8 is chosen on cost (analytic, no table, identical on CPU and GPU). This is a cost choice, not a finding that Bayer looks better.
- Anchoring. Every world-anchored cell fails the accuracy gate. A (the 4x4-box error against the continuous tone) is 8.2 to 15.5 for world cells against 2.4 for Bayer8 screen. The nearest cell score without the gate was close (screen 0.78, world-octave 0.85, world-fixed 0.95).

## What the numbers say (mean over 31 frame pairs, 256 x 144, 1 bit)

| path | cell | L surface lock | F boil | A error |
|---|---|---|---|---|
| dolly | bayer8 screen | 0.216 | 1.28 | 2.5 |
| dolly | bayer8 world-fixed | 0.191 | 11.3 | 10.1 |
| dolly | bayer8 world-octave (2 px) | 0.115 | 10.0 | 15.6 |
| strafe | bayer8 screen | 0.225 | 1.17 | 2.3 |
| strafe | bayer8 world-octave | 0.055 | 6.6 | 13.8 |
| yaw | bayer8 screen | 0.356 | 1.53 | 2.4 |
| yaw | bayer8 world-octave | 0.137 | 12.9 | 13.7 |

Reading it:
- A surface-attached pattern halves to quarters the surface-lock error L (strafe: 0.225 to 0.055) and moves 6 to 9 times more in screen space (F). Both follow from what anchoring means. F counts the pattern following the content as motion, so it penalises world anchoring by construction; the metric cannot say which of the two a viewer calls shimmer. That stays open until human raters see clips.
- The accuracy cost is a finding. Even with a 1 px cell target (exploratory run) a world-anchored pattern has A of 8.9 to 9.2 against 2.4 for screen: sampling a planar projection of the matrix at perspective positions does not keep the one-threshold-per-pixel density that makes ordered dither mean-preserving. The pixel-lattice property of the screen pattern is worth more than the lock.
- Octave snapping works as designed (seam fraction 1.7% on dolly, 0.04% to 0.25% on strafe and yaw), and it cuts L by 33% to 54% against world-fixed, at a further accuracy cost (cells of 2 to 4 px).
- The one-rater blind look (12 stills, key hash b6b65589...10451 fixed before looking; one rater who wrote the code, so not independent): I picked the screen-anchored cells as cleanest (s06, s08, s11) and saw coarse dots and moire fans on the near ground in the world-anchored cells. The blue-noise screen cell looked more like film grain, the Bayer screen cell cleaner on flat tone. Unblinded, those picks were screen cells.

## Prediction scorecard (65 hit, 36 missed of 101)

Misses, with causes where known:
- P1 (L within the model bound): the six screen cells hit. All world cells missed, running 1.4 to 2.4 times the model (dolly world-octave 0.115 against 0.050). The model assumed that only the sub-pixel residual of the reprojection moves a pixel across a cell edge. It leaves out drift of the reprojected anchor across a depth gradient and perspective stretching of cells, so it under-predicts. The model is not usable as a bound for world anchoring.
- P2: held on dolly, strafe and yaw for both sources, and on orbit.
- P3: held.
- P4: L within 10% held except strafe (screen, 0.225 against 0.272 for blue; octave 0.055 against 0.050). F(bayer) <= F(blue) missed by under 5% in five cells (a tie). A differed by 45% between sources at screen anchoring (bound 15%): blue noise is less accurate at the 4x4 scale.
- P5: static L = 0 held; static F was 0.07 to 0.09, not 0. Exploratory rerun with the sky shards frozen gives F = 0.00 in all cells, so the instrument is sound and the scene's drifting sky caused it. The displaced control gave 0.39 to 0.46 for Bayer against the independent value 0.33: the Bayer thresholds three pixels apart are anti-correlated, a property the independence assumption lacks. Blue was inside the bound.
- P6 and P7: P6 held on dolly (1.7%) and strafe and yaw; P7 missed (the winner is screen, not world-octave).

## Limits and next

One scene, one resolution, one frame rate, software metrics, one non-independent rater. The pre-registered F and L describe two viewing models and a 4x4 box is one scale. Next: clips with human raters for the screen versus world question, a temporal-hysteresis variant of the screen cell (the hysteresis module already exists), and object-only anchoring for isolated moving objects on a screen-anchored scene (the spin path: screen L 0.000, F 0.02).

## Demo look, second version (2026-10-15)

A reviewer's look at the first demo frames found the ground flat, the geometry blocky and the lighting short of drama, and asked for a more surreal composition closer to the reference stills. The second Salt Road (`web/shaders/fixtures/dream2*.mjs`, separate files so the evidence above stays reproducible) changes:
- Ground: cracked salt polygons, ripples, scattered stones, a pale road that winds to the lighthouse, and a small bump in the height field, all in the albedo and the geometry.
- Geometry: hanging gothic arches with brick courses and pillars, a fluted inverted spire with buttress fins and orbiting stones, a banded black and white ring, trees with a leaf-noise canopy (the forest reads as dither texture, as in the reference stills), floating octahedral shards, and a large moon lit from the side with craters.
- Light: a darker night (darkness level 0.02, down from 0.16), tighter key pools from the lantern and orbs, a flickering lantern, a sweeping beam, and a new per-pixel haze input to the lighting model: light scattered toward the camera along each view ray, integrated from the lights through a density that falls with height, screen-blended into the tone (`1 - (1 - tone)(1 - haze)`). It is in the CPU reference, the WGSL pass and the packer; two new parity cases on SwiftShader show 0 decisions differing, and zeroing the haze in the WGSL fails both (48.9% and 6.9% of pixels differ).
- Composition: a low camera under the arches and through the ring toward a bright passage, with the spire and shards overhead at the end; 1 bit first, then 4 tones.
Before and after stills are in `docs/images/ditherlight-*.png` and `ditherlight2-*.png`. Limits: the frames were judged by eye only; the beam shafts are visible but subtle at 1 bit; the large hanging slabs still read as heavy flat masses from below.
