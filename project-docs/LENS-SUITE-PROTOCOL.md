<!-- writing-profile: research -->
# Lens suite: protocol, predictions and bounds

Dated 2026-10-15. Committed before the study tool (`tools/shaders/lens_study.mjs`) and the GPU parity page exist. The bounds below and in `evidence/lens-protocol.json` and `evidence/shaders-lens-parity-bounds.json` do not move after the first run. A miss stays in the results with its cause; a fix gets an addendum with new bounds committed before the rerun.

## What is built

`web/shaders/lens` takes one scene-linear HDR frame and returns it with the light a lens adds. Every term is a pure function with a float64 CPU reference and a WGSL twin.

- Spectrum: eight 40 nm bins from 420 to 700 nm. Each bin carries an RGB weight from the CIE 1931 observer in linear Rec. 709, negative lobes clipped, scaled so each channel sums to 1 over the bins. A term that ignores wavelength returns the source's RGB unchanged.
- Thin-film coating: one layer (n1 1.38, a quarter wave at 550 nm) on crown glass, Airy summation, averaged over s and p. It sets the reflectance of every lens surface per bin and per angle of incidence. The ghosts and the anamorphic streak take their tint from it. No colour is painted on by hand.
- Ghosts: our own three-element lens (six surfaces, f = 43.0 mm, crown and flint glass by a two-term Cauchy formula). Every pair of surfaces gives one ghost (15). Paraxial ray-transfer matrices give each ghost's position factor m (centre at c + m (p - c)) and its size per bin, so ghosts split into colours by dispersion. The shape is the iris image, with an anamorphic squeeze. Ghost energy is ghostGain times R_i R_j at the source's field angle.
- Starburst: the Fraunhofer pattern of an N-blade iris (our own FFT of a 256 x 256 supersampled aperture), summed over the bins with each bin scaled by its wavelength, the Airy core windowed out. Spike size grows with the f-number.
- Anamorphic streak: exponential along the streak axis, gaussian across, per bin weighted by R(lambda) over its mean, so the coating sets the tint.
- Halation: a 2D exponential per source whose weight and radius rise toward red (light that reaches the film base and returns re-exposes the deepest layer).
- Lateral chromatic dispersion: the core image resampled per bin at a magnification from the crown glass's dispersion.
- Energy: every pixel of a kept source tile gives up phi (per channel) of its over-threshold light, and the terms add the same amount back, so the stage redistributes light. With a source list from the scene (RT mode) nothing is removed.
- Controls: `params.mjs` holds the engine-side terms; seven of them are keyed to the eight time-of-day slots through `web/look/keys.mjs`. No player UI.

## Predictions (CPU, float64)

Configurations are fixed in `evidence/lens-protocol.json`.

- P1 Energy. A 3 x 3 source of energy [300, 300, 300] at the centre of a 1025 x 1025 black frame, f/16, streak length 60 px, all terms on. Total output energy per channel equals the input within 1%. With dispersion on, the same bound.
  - Control C1: the same frame with the removal switched off (keep = null, sources still extracted) gains phi of the over-threshold energy, predicted +14% to +19% per channel, and must fail the 1% bound.
- P2 Spikes. The monochrome diffraction pattern of the iris (n 256, radius 24 cells), sampled on a ring of radius 40 cells, has local maxima more than 3 times the ring median at: 6 blades 6, 8 blades 8, 5 blades 10, 7 blades 14, a round iris (roundness 1) 0.
- P3 Ghost positions. For a source at a field angle of 0.2 degrees, the paraxial ghost centre of each of the 15 ghosts lies within 0.5 px (on a 1920 x 1080 frame, d line) of the centre found by an exact meridional ray trace (Snell's law at every spherical surface, written separately in `web/shaders/lens/exact.mjs`). Exploratory, no bound: the same comparison at 10 degrees, where I expect 1 to 30 px from third-order aberration.
- P4 Coating. R(550 nm, 0) equals the quarter-wave closed form ((ns - n1^2) / (ns + n1^2))^2 = 0.01261 within 1e-6. R(420) and R(700) each exceed 2 R(550) (the magenta residual). The reflectance minimum moves to a shorter wavelength at 30 degrees: predicted 513 nm plus or minus 8 nm (cos theta1 = 0.932 inside the layer).
- P5 Dispersion. A one-pixel white source at 0.9 of the half-diagonal, dispersion only (other gains 0, caAmount 0.5): the radial distance between the red and blue channel centroids equals the value predicted from the bin magnifications and the channel weights within 0.15 px.
- P6 Halation. Halation-only render of a white source: channel energies satisfy R > G > B, and R / B equals the table's prediction within 2%.

## GPU parity (SwiftShader)

Bounds in `evidence/shaders-lens-parity-bounds.json`, fixed now: at 192 x 108 on four cases, the 8-bit encodes of CPU and GPU differ by at most 3 codes, p99.9 at most 1 code, mean at most 0.03 codes. False-success control: flipping the sign of the ghost position factor in the WGSL must fail at least the cases that have off-centre sources.

## Look

I look at every frame I report. Critique of the frames is mine unless it is quoted with a name.

## Addendum A (2026-10-15, after the first study run, before the rerun of P2 only)

The first run missed P2 for 7 blades (0 spikes counted, 14 expected) and 8 blades (7 counted, 8 expected). An exploratory sweep (not evidence) showed the counter depends on the ring radius and the prominence threshold: with 7 blades it counts 0, 0, 1 and 14 spikes on rings of 20, 30, 40 and 50 cells. The Airy side-lobe rings dominate a single ring, so the instrument, not the pattern, decides the count. P2 stands as a miss in the results.

New instrument P2b, fixed now: integrate the monochrome pattern over the radial band 20 to 60 cells into an angular profile of 1440 samples, take its discrete Fourier series over angle, and find the harmonic k in 1 to 40 with the largest amplitude. Bounds: for 6 and 8 blades the dominant harmonic is k = N; for 5 and 7 blades it is k = 2N; for each bladed iris the dominant amplitude is at least 0.2 of the mean (k = 0); for the round iris every harmonic in 1 to 40 is below 0.05 of the mean. Same irises as P2 (roundness 0.15, rotation 0.3).

## Addendum B (2026-10-15, after the P2b run, before its rerun)

P2b passed for all four bladed irises and missed for the round iris: harmonic 1 at 0.116 of the mean against a bound of 0.05. A first harmonic means the profile is off-centre. Cause found in the instrument: `countSpikes` and `angularHarmonics` took the table centre as (n - 1) / 2, which is right for the odd starburst table but half a cell off for the even FFT table, whose zero frequency sits at n / 2. Both instruments now take the centre as an argument. The P2 and P2b bounds are unchanged; the rerun uses the corrected centre. The first P2 and P2b numbers stay in the results.
