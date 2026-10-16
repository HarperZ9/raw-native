<!-- writing-profile: research -->
# Palette-locked lighting: results

Run 2026-10-15 under `PALETTE-LIGHTING-PROTOCOL.md` (committed before the study tool existed). Raw numbers: `evidence/palettelight-study-runs.json`, GPU parity `evidence/shaders-palettelight-parity-swiftshader.json`.

## What was built

A 44-colour palette of ramps (four material classes, each with a cool and a warm ramp of four steps built in OKLab; a six-step cycling ramp; short window and lamp ramps), a lighting pass that resolves irradiance into those ramps, an original night scene (Chapel Yard), a WGSL pass, and a demo. Every output pixel is a palette entry by construction.

## Against quantising a continuous render afterwards

Same scene, same 44 colours, lamp sweep of 24 frames, 320 x 180. The post pipeline is the engine's palette dither (`mix-bayer8`) applied to a continuous render of class colour times coloured light.

| measure | palette-locked | post-dither |
|---|---|---|
| output is a palette entry | 1.00 | 1.00 |
| ramp purity (a lit pixel uses its own material's ramps) | 1.00 | 0.41 |
| look fidelity (OKLab distance to the ramp ideal, x100) | 1.27 | 9.69 |
| physical fidelity (distance to the naive continuous render, x100) | 8.79 | 1.09 |
| pixel changes per frame under the moving lamp | 0.0107 | 0.0569 |
| pixels changing material ramp per frame | 0 | 0.048 |

The two fidelity rows each favour the pipeline whose reference they use, by construction: the locked pass reproduces the authored ramps and the post-dither reproduces naive lighting. They show a trade, not a winner. The rows that do not depend on a reference are purity and the temporal rows: a post-quantised render puts 59% of lit pixels into another material's colours and flips 4.8% of them between materials each frame, where the locked pass does neither, and it changes 5 times fewer pixels per frame under a moving light. The hue shift of lamp-lit stone against moon-lit stone is 27 degrees. The mean ramp step equals the mean ramp coordinate within 0.002 steps in every class. The cycling ramp rotates exactly: at one cycle step later all 1305 void pixels move to the next entry.

## Scorecard

Seven of eight predictions held. Missed: the change rate. The prediction was a ratio between 0.7 and 1.3 of the post-dither; measured 0.19. The locked pass is calmer than predicted. A likely cause, not tested: its variant and step change only when the ramp coordinate or warm fraction crosses a threshold, while the post-dither's nearest-colour choices follow small changes in a continuous colour across many palette entries. Viewing the frames agrees: the post-dither shows red and blue speckle on grass and stone, the locked pass reads as coherent ramps with a yellow lamp pool.

## GPU parity

SwiftShader, bounds committed first: five cases, 0 palette indices differing in each after a fix. **The first run failed** with 2.3% to 4.7% of indices differing in all five cases. The cause was a packing bug: the radial-light parameters were 7 floats and the shader read an 8-float stride, so both window glows read shifted values. The packer was fixed and the bounds were not touched. A control (scaling the gain by 1.05 in the shader) fails all five cases.

## Look and limits

Frames of the demo (the moon crosses the sky and the shadows swing, the lamp and windows flicker, the void pools cycle) were viewed. The palette is authored by me and not by an artist; the night is dark and teal; the pools are bright. One scene, one palette, software metrics, a reference that is itself a choice, no human raters, and the study is of lighting a still camera (a moving camera is not measured here).
