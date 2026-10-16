<!-- writing-profile: research -->
# Ray-traced visibility through the dither threshold: results

Dated 2026-10-15. Predictions and bounds: [RT-DITHER-VISIBILITY-PROTOCOL.md](RT-DITHER-VISIBILITY-PROTOCOL.md), committed before any code (eee55cd). Runs: `evidence/rtvis-study-runs.json` (the protocol run) and `evidence/rtvis-explore-runs.json` (exploratory, after the protocol run, not evidence for it). Tool: `node tools/shaders/rtvis_study.mjs`, about 4 minutes on one thread.

## Result in one paragraph

One ray per pixel, with the light sample picked by the pixel's Bayer rank through a (0, 6, 3)-net, gives an unbiased, fully deterministic estimate of area-light visibility inside dither-quantised lighting. It does not give the accuracy I predicted. Its tile error beats one white-noise ray in two of three blockers, and it loses on the third. The net's samples are stratified on the light, but they are also tied to pixel position inside the tile. That makes the error depend on how the blocker's edge lines up with the light's sample axes. The headline hypothesis, that the dither pattern can be the estimator and so get one ray close to many, is not supported. It stays a negative result.

## Numbers

Penumbra tiles are 8 x 8 tiles with any pixel whose true visibility lies strictly between 0.02 and 0.98. Error is the tile mean of the output level's linear value minus the tile mean of the true tone.

| Blocker, levels | ref64 MAE | white1 MAE | rank1 MAE (bias) | net1 MAE (bias) | net1 / white1 | net1 / ref64 |
|---|---|---|---|---|---|---|
| wall along x, 2 | 0.0079 | 0.0171 | 0.0159 (+0.0034) | 0.0205 (+0.0074) | 1.20 | 2.6 |
| wall along x, 4 | 0.0044 | 0.0106 | 0.0134 (+0.0044) | 0.0152 (+0.0060) | 1.43 | 3.5 |
| wall along z, 2 | 0.0138 | 0.0244 | 0.0646 (+0.0471) | 0.0150 (-0.0053) | 0.61 | 1.09 |
| wall along z, 4 | 0.0070 | 0.0157 | 0.0317 (+0.0263) | 0.0108 (-0.0015) | 0.69 | 1.54 |
| sphere, 2 | 0.0101 | 0.0193 | 0.0375 (+0.0032) | 0.0174 (+0.0015) | 0.90 | 1.72 |
| sphere, 4 | 0.0046 | 0.0122 | 0.0171 (+0.0005) | 0.0152 (+0.0016) | 1.25 | 3.3 |

Against the predictions:
- Q0 net property: holds. All 28 elementary box shapes hold exactly one of the 64 points. A non-net control fails the same check (unit test).
- Q1 bias: holds. net1 and white1 stay inside plus or minus 0.01 everywhere (largest: net1 +0.0074, white1 -0.0091). rank1, the naive coupling, is biased by +0.047 on the wall along z, as the independence argument said it would be.
- Q2 accuracy: missed. net1 / white1 had to be at most 0.6 everywhere; it ranges from 0.61 to 1.43. net1 / ref64 had to be at most 2; it reaches 3.5.
- Q3 temporal: holds. On a static scene net1 changes 0 pixels between frames and white1 (reseeded) changes 9.0% of penumbra pixels. With the light moving 2 cm per frame, net1 changes 0.074% of penumbra pixels per frame, against 0.066% for ref64 (ratio 1.12, bound 1.5).
- Q4 cost: 1 ray per pixel against 64.

## Why Q2 missed (exploratory)

The swap variant exchanges the net's two light coordinates. It turns the worst case into the best: the wall along x drops from 0.0205 to 0.0120 at 1 bit and from 0.0152 to 0.0047 at 4 levels. The wall along z gets worse, from 0.0150 to 0.0289. The error is anisotropic. Inside a tile, a pixel's rank fixes both its threshold and its place on the light, and the rank is itself a function of the pixel's position. When the visibility gradient across the tile runs along the light axis that the low bits of the rank control, lit pixels and visible samples stay correlated even though the threshold and the sample are independent. A per-tile digital shift (netx) keeps the net property and averages the orientations. It is not better than white noise either: 0.0150, 0.0190 and 0.0190 at 1 bit. Four white samples (white4) land at 0.0125, 0.0188 and 0.0156.

The stills show this (`docs/images/rtvis-penumbra-wallx.png`; ref64, white1, rank1, net1 from left to right). Penumbra error from white1 reads as noise. From rank1 and net1 it reads as rows of tile-periodic marks along the shadow edge.

## What the RT path gains, then

The RT path does replace the scene's soft-shadow guess with a penumbra whose width comes from the light's size and the blocker's height, and that change is the real gain. The measured cost is per ray: one deterministic net ray per pixel is unbiased and stable under motion, and its tile error is 1.1 to 3.5 times that of 64 rays. For the night-harbour look I recommend 4 to 8 rays per pixel, with a decorrelated sampler that does not depend on the threshold. The net coupling is kept in the code for its zero flicker and as the recorded negative. The protocol tied the Salt Road RT version to Q2 holding, so it was not built.

## Limits

One analytic scene with three blockers, one light size, orthographic, 1 bit and 4 levels, software metrics only, no human raters. "Tile error" measures what a viewer integrates over 8 x 8 pixels. It does not measure how structured or noisy the error looks.
