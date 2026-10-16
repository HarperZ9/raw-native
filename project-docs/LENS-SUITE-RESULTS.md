<!-- writing-profile: research -->
# Lens suite: results

Dated 2026-10-15. Predictions and bounds: [LENS-SUITE-PROTOCOL.md](LENS-SUITE-PROTOCOL.md) (committed in daa0a42 before any measuring tool existed; Addendum A in f8e01ca and Addendum B after it, each committed before its rerun). Runs: `evidence/lens-study-runs-first.json` (the first run, kept), `evidence/lens-study-runs.json` (the run on the final code), `evidence/shaders-lens-parity-swiftshader.json`, `evidence/shaders-lens-control-swiftshader.json`.

## Result

The lens suite works as a measured, energy-conserving stage. Ghosts sit where an exact ray trace puts them. Spikes have the count that diffraction predicts. The GPU pass matches the float64 reference within one 8-bit code. Three predictions missed: a ring-based spike counter, the size of the coating's residual reflection at the ends of the band, and how far the paraxial ghost positions drift at 10 degrees. Each miss is recorded with its cause.

| Prediction | Outcome |
|---|---|
| P1 energy, all terms on, within 1% | holds: -0.002% per channel; with dispersion on, at most +0.22% (blue) |
| C1 control, removal off, must fail | fails as required: +15.8%, +13.2%, +17.7% (the final run; the first run gave +16.4, +13.6, +18.3). My range was 14% to 19%, and green missed it both times |
| P2 spikes, ring counter (6, 8, 5, 7 blades; round) | **missed**: 7 blades counted 0 against 14, 8 blades 7 (first run) or 0 (after the centre fix) against 8. The counter depends on ring radius and threshold (exploratory sweep). It is a weak instrument, and the miss stands |
| P2b spikes, angular harmonics (Addendum A, B) | first P2b: the round iris failed on harmonic 1 (0.116), traced to an instrument centre bug (the table centre was taken as (n - 1) / 2; the FFT table's centre is n / 2). After the fix, all five hold: dominant harmonic 6, 8, 10, 14; the round iris has every harmonic below 0.015 of the mean |
| P3 ghost centres, paraxial vs exact trace, 0.2 degrees, within 0.5 px | holds: largest 0.0008 px over 15 ghosts |
| P3 exploratory, 10 degrees, expected 1 to 30 px | **missed**: up to 121 px (ghost of surfaces 1 and 0); the main image itself drifts 4.7 px. At 10 degrees this lens is far outside the paraxial regime |
| P4 coating | closed form holds (0.0126008 both ways); the 30-degree minimum moves to 512.75 nm (predicted 505 to 521). **Missed**: the band ends reflect 1.93% (420 nm) and 1.60% (700 nm), not more than twice the 1.26% at 550 nm. I overestimated how steeply a single layer rises |
| P5 dispersion, red-blue centroid distance within 0.15 px | holds: 0.0016 px from the prediction (-1.741 px measured) |
| P6 halation, R > G > B and R/B within 2% | holds: R/B 132.7 against 133.0 |
| GPU parity, SwiftShader, 4 cases, max 3, p99.9 1, mean 0.03 codes | holds: max 1, p99.9 0, mean at most 0.0008 on the final code |
| False-success control: ghost position sign flipped in the WGSL | fails every case (max 4, 24, 20, 9 codes), as required |

## What it looks like

`docs/images/lens-quay-before.png` and `lens-quay-after.png` show the same Quay Street frame without and with the stage. `lens-quay-close.png` has the van's headlights close to the camera. `lens-quay-slots.png` shows one frame through each of the eight slots' lens controls: at noon the lens stops down to f/11, so ghosts shrink, spikes lengthen and fewer sources pass the threshold. The demo is 144 frames at 24 fps, with a van driving toward the camera on a wet harbour street.

My reading of the frames, as the one rater:
- The anamorphic streaks carry the look. With the coating's quarter wave at 600 nm, the residual reflection is blue-violet and the streaks read cool against sodium-orange lamps, without any hand-set tint.
- Close to the camera, the ghosts from the headlights are large, soft polygons. They are faint at the night gain, and the gain is a stated handle, not physics.
- Weak points: the shop windows are flat panels, faint arc shading is left on the right facade from the lamp's soft shadow, and the van is a block-out.

## Limits

- The coating is applied with the air-side formula at every surface, including glass-to-air surfaces inside the lens. Ghost energy at off-axis sources follows the coating at that angle, while the energy ledger uses normal incidence, so off-axis sources are not exactly conserved.
- Element edges (vignetting of ghosts) are not modelled, and the ghosts use the paraxial regime. The 10-degree result shows where that breaks.
- The streak length, the gains and the core window of the starburst are handles. Real lights are many stops brighter than a display-referred demo can show.
- Software metrics and one rater. The search for prior art could not reach the ACM Digital Library or Shadertoy (see NOVELTY.md).
