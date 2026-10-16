<!-- writing-profile: research -->
# Low-pressure sodium light: results

Dated 2026-10-16. Predictions and bounds: [SODIUM-LIGHT-PROTOCOL.md](SODIUM-LIGHT-PROTOCOL.md), committed before any code (65b5331). Runs: `evidence/sodium-study-runs-first.json` (the protocol run), `evidence/sodium-study-runs.json` (the same run repeated on the final scene, identical numbers), `evidence/sodium-explore-runs.json` (exploratory, after the protocol run), `evidence/shaders-sodium-parity-swiftshader.json`, `evidence/shaders-sodium-control-swiftshader.json`.

## Result

The spectral model does what the physics says. Under pure low-pressure sodium, eight strongly coloured materials end up with the same chromaticity. Their spread is 0.0001, against 0.64 under a warm incandescent window. The GPU pass matches the float64 reference within one 8-bit code. Two predictions missed. Q4 missed by a hair. Q5, the distance prediction, missed by 1.27 m, because my model counted only the window as broadband light, and the sky fill and the van's LEDs also bring colour back. In this scene the sky fill keeps some colour visible even right under the lamp (spread 0.28). For the game mechanic, that means the sky fill has to stay low wherever sodium should erase colour.

| Prediction | Outcome |
|---|---|
| Q1 pure sodium, S below 1e-3 | holds: 9.6e-5 |
| Q2 sodium with 2% impurity lines, S below 0.03 | holds: 0.0116 |
| Q3 window (2700 K) alone, S above 0.15 | holds: 0.644 (LED 0.569, sky 0.727) |
| Q4 mixing: monotone; S(0.5) / S(1) in [0.3, 0.7]; f50 in [0.25, 0.6] | **missed** on one part: monotone yes, f50 = 0.274 yes, S(0.5) / S(1) = 0.703 (bound 0.7) |
| Q5 half-recovery distance along the tile strip, within 0.3 m of the inverse-square prediction | **missed**: predicted 2.82 m, measured 1.55 m (shadows on). Without shadows: 1.69 m. With mist and wet reflection (report only): 3.37 m |
| GPU parity, SwiftShader, 4 cases: max 2, p99.9 1, mean 0.03 codes | holds: max 1, p99.9 0, mean at most 0.0002 |
| False-success control: WGSL reads the window row for sodium lights | fails every case (max 82 to 158 codes), as required |

## Why Q5 missed (exploratory, not evidence)

The prediction used the window's share of the illuminance and Q4's two-light curve. On the strip, the sky fill (ambient 0.03, a 4100 K spectrum) and the van's white LED headlights add broadband light too. So the colour comes back sooner than the window share alone says (1.55 m, not 2.82 m). Shadows account for only 0.13 m of the gap. Two exploratory reruns:
- Counting every non-sodium light as broadband: d50 is predicted at 2.39 m. That is closer, but the curve still comes from a two-light mix.
- With the sky fill lowered to 0.005: the measured d50 is 2.54 m against 2.74 m predicted (inside 0.3 m), and the spread under the lamp falls from 0.28 to 0.13.

The demo uses a sky fill of 0.006 for this reason (stated in `tools/shaders/sodium_demo.mjs`).

## What it looks like

`docs/images/sodium-quay-spectral-vs-rgb.png` shows the same frame twice. The top half is spectral. The bottom half is lit the RGB way: a sodium-coloured RGB light times an RGB albedo. Under the spectral lamps, brick, doors and the near tiles of the painted strip are one orange. In the RGB version, the brick stays red and the tiles keep red and green. `docs/images/sodium-quay-strip.png` is the last frame of the walk, where the strip reaches the shop light and its colours come back. The demo is 120 frames: the camera walks along the strip in rain while a van with LED headlights drives past.

My reading of the frames, as the one rater:
- The mechanic reads: the tiles under the lamp are one colour, and the tiles near the shop are not.
- The van's LED headlights give a white pool and white reflections against the orange street, which is the mixed-light contrast the scene needs.
- Weak points: the closed shopfronts are flat panels, the van is a block-out, and the rain is a screen-space pattern lit by the mist, not particles.

## Method notes and limits

- Lines are exact delta terms, and continuous spectra are integrated at 1 nm with the CIE 1931 tables shared with the lens suite. The lens suite's 40 nm bins cannot hold the 0.6 nm doublet. A sodium lamp seen through the lens suite is still split by its RGB weights, which a follow-up should fix (a per-source bin spectrum).
- The response table is exact only for Lambertian, non-fluorescent single bounces. The wet-ground reflection is spectrally flat (Fresnel), so it stays exact. Interreflections between coloured surfaces are not modelled.
- Sodium light lies outside the Rec. 709 gamut, so it has negative blue in linear light, clipped at the display encode.
- The impurity fraction (2%) and its two weak lines are approximate figures.
