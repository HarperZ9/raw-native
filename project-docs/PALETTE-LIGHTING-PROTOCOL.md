<!-- writing-profile: research -->
# Palette-locked lighting: comparison protocol

Written and committed on 2026-10-15 before the study tool exists or runs. The code under test (`web/shaders/palettelight/`, `web/shaders/fixtures/chapel*.mjs`) and its unit tests exist at this commit.

## What is tested

Palette-locked lighting (EFFECTS-PROGRAM item 2, 2.5D pixel template): light is resolved into the palette's own ramps. A palette is 44 colours: for each of four material classes, two ramps of four steps (cool light, warm light) built in OKLab so shadows turn blue-violet and lights turn warm; a six-step cycling ramp for the void pools; short ramps for the lit windows and the lamp. Per lit pixel the irradiance gives a ramp coordinate and a warm fraction; the ramp variant (warm above 0.7, cool below 0.3, dithered between) and the step are chosen by thresholds from a Bayer 8 x 8 matrix at two phases. Every output pixel is a palette entry by construction.

The question: against the usual alternative, a continuous render quantised afterwards by the engine's palette dither (`dither` module, mode `mix-bayer8`, the same 44 colours), does lighting in palette space give a more coherent picture, and what does it cost?

## Stimuli

Chapel Yard (original, `chapel.mjs`), orthographic three-quarter camera, 320 x 180. Lit classes: ground, stone, wood, foliage. Lamp sweep: a roaming warm light at height 3.6 m moves along z = 5.6 from x = 2.5 to 9.5 in 24 frames (shadows recomputed per frame); time fixed at 0. The continuous render given to the post-dither pipeline is class colour times coloured light (cool and warm), tone mapped and encoded to sRGB; void and emissive classes are given their ideal ramp colour (the cycling ramp interpolated in OKLab) so both pipelines see the same animated void.

## Metrics (defined in tools/shaders/palettelight_study.mjs, written after this note)

- Exactness: the fraction of output pixels whose colour is a palette entry.
- Ramp purity: on lit-class pixels, the fraction whose output entry belongs to the pixel's own class (either variant).
- Look fidelity: 100 x mean OKLab distance, after a 4 x 4 box in linear light, between the output and the ramp ideal (the continuous colour the two ramps interpolate to at the pixel's coordinate and warm fraction), on lit-class pixels. This measures how well a pipeline reproduces the authored palette look. It favours the palette-locked pass by construction and is reported as that, not as general fidelity.
- Physical fidelity: the same distance between the output and the naive continuous render. This favours the post-dither pipeline by construction.
- Change rate: over the lamp sweep, the fraction of lit-class pixels whose output entry changes between consecutive frames.
- Class flips: the fraction whose entry changes to a different ramp class between consecutive frames.
- Hue shift: the mean absolute difference in OKLab hue between stone pixels within 1.5 m of the lamp's floor point and stone pixels the lamp does not reach, at lamp x = 6.
- Index mean: for each lit class over the frame at lamp x = 6, the absolute difference between the mean output step and the mean continuous ramp coordinate.
- Cycle rotation: for void pixels, the fraction whose index at time t + one cycle step equals the index at t plus one, wrapped.

## Predictions and bounds (fixed now)

- S1. Exactness is 1.0 for both pipelines.
- S2. Ramp purity is 1.0 for the palette-locked pass and at most 0.85 for the post-dither.
- S3. Look fidelity of the palette-locked pass is at most 0.7 times the post-dither's.
- S4. Physical fidelity of the post-dither is at most 0.9 times the palette-locked pass's (the post-dither is closer to the naive render; this is the cost of locking).
- S5. Change rate of the palette-locked pass is within plus or minus 30% of the post-dither's. Class flips: exactly 0 for the palette-locked pass, at least 0.02 for the post-dither.
- S6. Hue shift of the palette-locked pass is at least 12 degrees.
- S7. Index mean differs by at most 0.02 steps in every lit class.
- S8. Cycle rotation is exactly 1.0.

## What would falsify the plan

If S8 or S1 fail the pass is wrong. If S2 holds for the post-dither at 1.0 the comparison has no coherence gap to explain. If S3 and S4 both fail in the same direction, the metrics do not separate the two.

## Limits

One scene, one palette, software metrics, a naive-render reference that is itself a choice, a study palette authored by me and not by an artist, no human raters. The look is judged by viewing frames, and no claim of preference is made.
