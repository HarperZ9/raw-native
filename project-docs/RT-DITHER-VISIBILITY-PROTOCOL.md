<!-- writing-profile: research -->
# Ray-traced visibility through the dither threshold: protocol and predictions

Dated 2026-10-15. Committed before the estimator code, the scene and the study tool exist. Bounds below and in `evidence/rtvis-protocol.json` do not move after the first run; a miss stays in the results.

## The question

Dither-quantised lighting (`web/shaders/ditherlight`) turns a tone into one of N levels by comparing it with a per-pixel threshold from an 8 x 8 Bayer tile. Its visibility input today comes from a soft-shadow heuristic in the scene fixture: a distance-field march that guesses a penumbra and is not tied to the size of any light. A ray tracer can give the true visibility of an area light, but one ray per pixel is a coin flip and many rays cost many rays.

Which effect gains most from the RT path? I pick this one, for three reasons. The output is already a pattern, so the noise of one ray can hide inside it if the ray and the pattern are arranged together. The tile mean is what a viewer integrates, so the estimator only has to be right on average over 64 pixels. Low Water's night harbour lives on hard lamp light and long penumbrae, and a wrong penumbra width is visible at once in 1 bit.

## The estimator

Each pixel of a tile has a Bayer rank r in 0 to 63. Its threshold is T = (r + 0.5) / 64 as today. The same rank picks the pixel's one light sample: point k = r of a three-dimensional (0, 6, 3)-net in base 2, (r / 64, phi(r), psi(r)), where phi is the bit reversal of the 6-bit rank and psi applies the Pascal matrix mod 2 to the reversed bits. The first coordinate is the threshold; the other two place the sample on the light's disc by a concentric map. Over any tile the 64 (threshold, sample) pairs then fill every elementary box of volume 1/64 exactly once. The light sample is stratified on the disc and independent of the threshold at the resolution of the tile. One shadow ray per pixel, deterministic, no frame-to-frame reseeding.

Why it should be unbiased. With a binary visibility V-hat and a threshold independent of it, the expected level of a pixel is V Q(Y1) + (1 - V) Q(Y0), and the quantiser's expectation over the threshold is linear in tone, so the tile mean equals the tone with the true visibility V. Coupling the sample to the threshold naively (sample index = rank, laid out on an 8 x 8 grid) breaks the independence: where the blocker edge lines up with the coarse bits of the rank, lit pixels and visible samples go together and the mean moves.

## Methods compared

- truth: visibility from 4096 stratified light samples per pixel, then the continuous tone Y (no dither).
- ref64: visibility from 64 stratified samples, then the dither (64 rays per pixel).
- white1: one sample per pixel from a hash of (x, y, frame), then the dither.
- rank1: one sample per pixel at grid cell (r mod 8, r div 8) of the light (the naive coupling).
- net1: the (0, 6, 3)-net estimator above.

Scene "Penumbra Yard" (our own, analytic, exact ray intersection): a ground plane seen from above, orthographic, 256 x 256 pixels over 8 x 8 m, a spherical light of radius 0.6 m at 3 m, and a blocker: a thin wall along x, the same wall along z, or a sphere. Ground albedo 0.8, ambient 0.1. Levels 2 (1 bit) and 4.

## Predictions

Penumbra tiles are the 8 x 8 tiles whose truth visibility has any pixel strictly between 0.02 and 0.98. Tile error is the tile mean of the output level's linear value minus the tile mean of truth Y.

- Q0 Net. The 64 points of the estimator form a (0, 6, 3)-net in base 2: every elementary box of volume 1/64 holds exactly one point (unit test, all 28 box shapes).
- Q1 Bias. Mean signed tile error over penumbra tiles: net1 and white1 within plus or minus 0.01 in every scene and level count; rank1 outside plus or minus 0.03 in at least one of the two wall orientations.
- Q2 Accuracy. Mean absolute tile error over penumbra tiles: net1 at most 0.6 times white1, and at most 2 times ref64, in every scene and level count.
- Q3 Temporal. Static scene, 8 frames: net1 changes 0 pixels between frames; white1 (reseeded each frame) changes at least 5% of penumbra pixels. Light moving 2 cm per frame over 16 frames: the fraction of penumbra pixels that change level per frame for net1 is at most 1.5 times that of ref64.
- Q4 Cost. Rays per pixel: net1 1, ref64 64 (counted, not timed).

## Then

If Q1 and Q2 hold, the Salt Road demo gets a version with net1 visibility from the distance-field scene in place of the soft-shadow heuristic, shown beside a 64-ray reference and the heuristic. That comparison is a look, with no bound.
