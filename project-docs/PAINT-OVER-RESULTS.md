<!-- writing-profile: research -->
# Paint-over projection: results

Run 2026-10-15 under `PAINT-OVER-PROTOCOL.md` (bounds first, commit e54ed9d; Addendum A before the second run, commit ef5db54). Raw numbers: `evidence/paintover-roundtrip-runs.json`, `paintover-offaxis-runs.json`, `paintover-keyframes-runs.json`. Post hoc diagnostics (they decide nothing): `paintover-holes-exploratory.json`.

## What was built

An original harbour block-out ("Lamp Quay", signed distance fields), an orthographic isometric camera, a procedural painter (layered brush strokes clipped to objects, impasto relief, ink lines; original code; the layered-stroke idea is Hertzmann's, 1998), projectors with depth-map visibility, keyframe blending by view angle, a greedy secondary-projector search, an 8-slot time-of-day grade from the look keys, a WGSL projection pass with SwiftShader parity, and a demo video.

## Round trip (render, paint, project, render again from the same camera)

At 960 x 540 with the 1152 x 648 overscan painting: **maximum difference 0 codes, mean 0.00000** on every pixel the projector covers (bounds 1 and 0.02). Covered fraction 0.99679 (the 0.3% left are silhouette pixels; bound 0.995). Controls: a one-pixel camera shift gives a mean of 3.2 codes (needed 2.0), and a depth map offset by one unit covers nothing (needed at most 0.01). One control missed its bound: the unpainted lit export differs from the painting by a mean of 4.35 codes, under the 5.0 predicted, because the painting stays close to its own export. The control still separates 4.35 from 0.

## Off-axis views (480 x 270, orthographic)

Stretch of the primary painting over the quay ground, measured from the G-buffer and compared with an analytic plane model made from the cameras alone: texel magnification and anisotropy match within 3% in 17 of 18 cases (one miss: elevation -10, magnification 1.037 against 1.000). The model values at yaw offsets 0, 5, 10, 20, 30, 45, 60 degrees are magnification 1.00, 1.05, 1.11, 1.22, 1.33, 1.49, 1.62 and anisotropy 1.00, 1.11, 1.22, 1.48, 1.77, 2.22, 2.62. Walls at 30 degrees: magnification 1.38, anisotropy 1.78.

Holes are pixels no projector claims; they show the unpainted block-out.

| yaw offset | primary, first run | primary, overscan | three keyframes |
|---|---|---|---|
| 5 | 0.065 | 0.024 | 0.0015 |
| 10 | 0.119 | 0.044 | 0.0014 |
| 20 | 0.207 | 0.094 | 0.0025 |
| 30 | 0.277 | 0.154 | 0.0096 |
| 45 | 0.352 | 0.225 | 0.055 |
| 60 | 0.391 | 0.266 | 0.095 |
| 90 | 0.474 | 0.288 | 0.166 |

The first run's holes were about three times the prediction. The causes were a finite painting frame (the water plane runs past it: 4% of pixels at 5 degrees and 16% at 30) and hidden-behind-geometry holes (2% and 10%). Overscan removes most of the first kind. Two hole-filling secondaries chosen by the greedy search (yaw +30 and -30, elevation -10; they cover 68% and then 26% of the 81,880 missed points) bring a 30 degree view down to 0.96% holes.

## Keyframe views, copy exactness, seams

From each of the three projector cameras the rendered view equals that projector's painting (maximum 0 codes, mean 0.00000, more than 516,000 pixels each). This held only after a fix. The first pass of the tool looked green but had compared 0 pixels for the secondaries: a stale cache in the weight code gave NaN weights, and the check passed vacuously. The cache is fixed and the check now fails below 50,000 compared pixels. Secondary paintings equal the primary's projection away from the holes (maximum 0). Where two projectors overlap, their colours differ by a median of 1 code and a 95th percentile of 10 to 12 (bounds 6 and 25).

## What missed

- The pop criterion, amended after the first run to a 99.9th percentile of at most 4 codes per degree and a maximum of at most 16: measured 99.9th percentile up to 4.44 and maximum 29.3. The large values are isolated pixels on ink lines and edges, where two projectors resample a thin line differently.
- From the first run: P2 (primary-only holes), P6 (a wrong check: it counted pixels the primary does not cover), the pop rule, and R4. All are recorded in PAINT-OVER-PROTOCOL.md, Addendum A.

## Look and limits

The frames were viewed. The orbit of plus or minus 35 degrees keeps the painting attached to the surfaces, and the day sweep (midnight to midnight, the 8-slot grade) reads as dawn, noon, golden hour, dusk and night, with lamp glow. One visible artefact: the ink outlines baked into the primary painting project onto the water behind the quay at off-axis angles, as thin grey curves. Paint-over cannot parallax a drawn line.

Limits: one scene; signed-distance block-outs, not triangle meshes; orthographic views only in the measurement (the GPU parity includes a perspective view); a procedural painting; one non-independent viewer.
