<!-- writing-profile: research -->
# Low-pressure sodium light: protocol and predictions

Dated 2026-10-16. Committed before the spectral module, the scene, the shading pass and the study tool exist. Bounds below, in `evidence/sodium-protocol.json` and in `evidence/shaders-sodium-parity-bounds.json` do not move after the first run. A miss stays in the results.

## Why

The author decided on 2026-10-16: "Sodium lights, yes". A low-pressure sodium lamp puts nearly all of its visible light into the D lines at 589.0 and 589.6 nm. Under it every surface reflects the same wavelength, so every surface has the same chromaticity, and colour is gone. Next to a shop window or under headlights, the colour comes back. For Low Water that is a mechanic: the colour of a clue reads only when it is carried out of the sodium pool. An RGB renderer cannot show this, because a sodium-coloured RGB light times a red surface is still a red-orange surface.

## Method

Each light has a spectral power distribution (SPD). Each material class has a spectral reflectance. For Lambertian reflection with no fluorescence, the reflected colour of a material under a light is fixed, so the module integrates, once and in float64, the response of every light type on every material:

  resp[m][L] = integral of S_L(lambda) R_m(lambda) cmf(lambda) d lambda, in linear Rec. 709, with lines as exact delta terms.

A light's white response (R = 1) has luminance 1 per unit intensity. A shading pass, on the CPU in float64 and on the GPU in WGSL, sums resp[m][L] times the light's geometric term over the lights. It uses the same CIE 1931 tables and the same Rec. 709 matrix as the lens suite (`web/shaders/spectral.mjs`). The lens suite's 40 nm bins cannot hold a 0.6 nm doublet, so the light module integrates lines exactly instead of binning them. The colours stay in linear Rec. 709 without clipping until the display encode. A sodium colour lies outside the Rec. 709 gamut, so its blue component is negative.

Lights:
- sodium: D2 at 589.0 nm and D1 at 589.6 nm in a 2 : 1 ratio, plus an impurity fraction in weak sodium lines near 568.5 and 615.8 nm (default 2%, an approximate figure).
- window: a 2700 K blackbody (a warm interior).
- LED: a blue band at 450 nm plus a broad phosphor band at 560 nm (headlights).
- sky: a 4100 K blackbody at low level.

Materials: eight of our own reflectances of the sigmoid-of-a-quadratic form (Jakob and Hanika 2019 is cited for the form; the coefficients are ours): red paint, orange buoy, yellow line, green door, teal hull, blue van, violet, and a flat 50% grey.

Metric. Chromatic spread S of a light: the largest distance, over pairs of palette materials, between their OKLab chromaticity directions (a / L, b / L), computed from XYZ under that light with no white balance. When S is 0, the materials differ only in lightness.

## Predictions

- Q1 pure sodium (impurity 0): S below 1e-3.
- Q2 sodium at the default 2% impurity: S below 0.03.
- Q3 window light alone: S above 0.15.
- Q4 mixing: sodium and window light are mixed with a window fraction f of luminance, f from 0 to 1 in 21 steps. S rises monotonically in f. S(0.5) / S(1) lies between 0.3 and 0.7. The half-recovery fraction f50, where S first reaches half of S(1), lies between 0.25 and 0.6.
- Q5 distance. On the pavement strip of the scene, running from under a sodium lamp toward a lit shop window, the inverse-square and cosine model (no shadows, no mist) gives the window fraction f(d), and Q4's measured f50 then gives a predicted half-recovery distance d50. In the rendered direct-light pass (shadows on, mist off), the measured d50 of S along the strip lies within 0.3 m of the prediction. For the full render with mist and wet reflections, I report d50 with no bound.
- Q6 GPU parity (SwiftShader): see the bounds file. Max 2 codes, p99.9 1 code, mean 0.03 codes, over four cases. False-success control: in the WGSL, reading the response row of the window instead of the sodium light must fail every case that has sodium light.

## Look

I look at every frame I report. Critique is mine unless quoted with a name.
