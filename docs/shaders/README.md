# The shader library

raw-native's own shaders for retro and film looks, built from the physics of the thing they imitate. A tube is simulated as a tube: a video signal through a decoder, electron beams, a phosphor mask and faceplate glass. Film is simulated as film: a negative exposed through its layers, developed, printed and projected. Every GPU pass has a CPU reference that it matches within one 8-bit code, and each physical constant is listed with its source and confidence in [CONSTANTS.md](CONSTANTS.md).

Every line is original. GPL shaders (crt-royale, crt-geom, crt-guest-advanced and the rest) were not read for code. Published papers and standards are cited where they are used.

## Run it now

```
python -m http.server 8000         # any static server that sends .mjs as text/javascript
# open http://localhost:8000/web/shaders/showcase.html in a WebGPU browser
node --test web/shaders/crt.test.mjs web/shaders/film.test.mjs
python tests/web/shaders_parity.py --shader crt --adapter swiftshader
python tests/web/shaders_parity.py --shader film --adapter swiftshader
node tools/shaders/sheets.mjs build/shader-sheets          # comparison images from the CPU references
```

## Using it

```js
import { createHost } from "./web/raw-gpu.mjs";
import { createGpuCrt } from "./web/shaders/crt/gpu.mjs";
import { createGpuFilm } from "./web/shaders/film/gpu.mjs";

const host = await createHost({ canvas });
const tube = await createGpuCrt(host, "slot-tv", { signal: { standard: "pal" } }, { w: 256, h: 224 }, { w: 1920, h: 1440 });
tube.frame(sourceRGBA, frameNo);   // Float32Array of R'G'B'A signal values, source size
// tube.image: linear RGBA f32 per pixel (1 = the tube's peak white); tube.packed: sRGB RGBA8

const film = await createGpuFilm(host, "500t-no-remjet", { ev: 0.5 }, { w: 1920, h: 1080 }, { w: 1920, h: 1080 });
film.frame(sceneLinearRGBA, frameNo);   // scene-linear Rec.709 in, display-linear Rec.709 out
```

The CPU references have the same shape: `createCrt` in `web/shaders/crt/crt.mjs` and `createFilm` in `web/shaders/film/run.mjs`.

## The tube (`web/shaders/crt/`)

Presets: `pvm-20` (aperture grille, RGB), `trinitron-tv` (grille, composite, comb filter), `slot-tv` (slot mask, composite, notch filter), `vga-14` (delta mask), `bw-tv` (P4 white), `radar-p7` (P7 blue flash and yellow-green afterglow), `scope-p31`. Any field can be overridden.

| Pass | What it simulates |
|---|---|
| `signal.encode`, `signal.separate`, `signal.demod` | The source line sampled at four times the subcarrier as a DAC holds it. RGB goes through the amplifier bandwidth. S-video and composite encode Y'IQ or Y'UV, band-limit the chroma and modulate it onto the subcarrier, whose phase advances per sample, line and frame. The decoder separates luma and chroma with a notch or a one-line comb, then demodulates. Dot crawl, rainbowing and hanging dots are what the decoder gets wrong, so nothing paints them on. |
| `beam` | Each gun's current follows the tube's power law (BT.1886). A gaussian spot grows with current, so dark lines show gaps and bright lines close them. Each deposit is integrated exactly over the output pixel (erf), so line light is conserved at any resolution. Convergence error is static plus radial. Persistence uses two exponential terms per phosphor with a history buffer, and each line decays from the time the beam passed it. |
| `beam` (mask) | Aperture grille with damper wires, slot mask or delta triads, in millimetres on the face. A tent filter integrates it over the pixel footprint, so it does not moire. Light is normalised to the mask's mean, so stripe peaks exceed 1 the way a real tube's do. |
| `halo.down`, `halo.conv` | Faceplate halation, derived from Lambertian emission into glass, Fresnel and total internal reflection at the front surface, tint absorption along the path, and three bounces off the phosphor layer. The ring's inner edge sits at 2 t tan(asin(1/n)), about 21 mm on a 12 mm faceplate. |
| `compose` | Phosphor colour from emission spectra or standard primaries, white-balanced (D65 or 9300 K). Gamut is mapped by desaturating toward luminance. The room's light raises the black level. Curved glass reflects a soft window with Fresnel weighting. Rounded face and bezel. |
| `encode8` | Display encode with a soft shoulder above 0.8, then sRGB. |

Curvature is perspective: the viewer looks at a paraboloid face (spherical, cylindrical or flat), so the picture bends the way a tube's does when you look at it.

## Palette and dither (`web/shaders/dither/`)

Quantises an 8-bit plate to a palette in OKLab with no dither, Bayer 2, 4 or 8, interleaved gradient noise, or blue noise. The pick runs between the two palette colours that bracket the pixel. The reference is the Studio's own `retro-dither.js`, vendored unchanged in `web/shaders/reference/`. The GPU pass makes every decision (nearest entry, bracketing pair, threshold) in double-single arithmetic, a pair of f32 that carries about 48 bits. Its palette indices therefore equal the f64 reference's. Checked on 76 cases and on every one of the 16,777,216 sRGB colours for two palette and mode pairs, with zero mismatches. The blue-noise mask is a 64 x 64 void-and-cluster array (Ulichney 1993), generated by `bluenoise.mjs`.

```js
const q = await createGpuDither(host, { palette: "pico8", mode: "blue", strength: 1 }, { w: 320, h: 180 });
q.frame(plateRGBA8);   // q.packed: RGBA8 palette colours; await q.read() -> { idx, rgba }
```

## crt-classic (`web/shaders/crt-classic/`)

The Studio's tube stage (`retro-crt.js`) ported to the GPU pass for pass: beam scanlines and masks in 8.8 fixed point, bloom and halation in linear light, then warp, bezel, colour separation and vignette. On the committed frame set it is within one 8-bit code of `retro-crt.js`, and bit-equal where only integer paths run. Use it where a render must match the Studio exactly; use the physical tube for everything else.

## Paint (`web/shaders/paint/`)

Turns a rendered frame into a painting. Presets are media: `oil-grotesque` (template (a): thick, warped, outlined, broken colour, violet shadows against hansa lights), `oil`, `gouache` and `watercolour`.

| Pass | What it does |
|---|---|
| `prep` | Scene light to display light with a soft shoulder (`exposure`). Each colour becomes a pigment latent: concentrations of four pigments (titanium white, hansa yellow, quinacridone magenta, phthalo blue) from a 17^3 lookup solved by Levenberg-Marquardt, plus the RGB residual, so an unmixed colour decodes back to itself. The latent follows Sochorova and Jamriska 2021, implemented independently. |
| `tensor`, `tensor.x`, `tensor.y` | Structure tensor (Sobel), smoothed: local flow and anisotropy. |
| `akf` | Anisotropic Kuwahara (Kyprianidis, Kang and Doellner 2009; polynomial sector weights, NPAR 2010), written from the papers. It averages pigment latents, so neighbouring colours mix as paint (blue and yellow make green). |
| `stroke` | Line integral convolution of bristle noise along the flow (relief), and of a coarse field (stroke identity, for broken colour). |
| `relief` | Per pixel: XDoG lines (Winnemoeller et al. 2012), the lit impasto (shade and sheen from the stroke relief and a canvas weave), and the watercolour wet edge. |
| `compose` | A slow noise warp for the expressive looks. Complementary temperature in pigment space, per-stroke pigment jitter, and soft value bands. Then Kubelka-Munk decode. The medium comes last: lit impasto for oil and gouache; for watercolour, a Kubelka-Munk glaze over paper, thicker in the paper's valleys (granulation) and at wash edges. |

Everything textural is anchored to `canvasOffset`, so a camera pan carries the paper and brushwork with the world. On a one-pixel pan the mean frame-to-frame difference is 0.000 to 0.007 codes with the canvas anchored and 0.6 to 8.3 codes screen-locked (`paint.test.mjs`). Rotation, zoom and parallax need motion vectors, which are not done.

## The film (`web/shaders/film/`)

Presets: `500t-print`, `500t-no-remjet` (CineStill-style halation), `250d-print`, `bleach-bypass`. Controls include `ev`, `printerPoints` (0.025 log E each), `grainScale` (cloud size), `grainAmount`, `gateWidthMm`, `halationReach`, `weaveUm` and `jitterUm`.

| Pass | What it simulates |
|---|---|
| `expose` | Gate weave (slow drift plus pull-down jitter). The scene is upsampled to a spectrum on a smooth basis that reproduces its XYZ exactly, and the negative's three layers are exposed through their spectral sensitivities, filtered so white exposes equally. |
| `mtf.x`, `mtf.y` | Emulsion scatter, the film's own MTF: a gaussian of sigma 4.2 um (`emulsionSigmaUm`), about 45 cycles/mm at 50%. Skipped below 0.15 pixel. |
| `halo.down`, `halo.conv` | Halation. Light reaching the 0.125 mm base comes back by Fresnel and total internal reflection, with the red layer first. The remjet absorbs it. Without the remjet a ring appears at 2 t tan(theta_c) = 0.22 mm. The camera's pressure plate (`pressurePlate`, diffuse reflectance, default 0.05) scatters back the sub-critical light that leaves the base. That light lands inside the ring and adds to the Fresnel fill there. Set it to 0 for the bare ring. |
| `develop` | Characteristic curves, then the interimage (DIR) gain, then the developed dye fraction per layer. |
| `print` | Grain on the negative from the Boolean model (Newson et al. 2017). Where clouds are near pixel size it uses Monte Carlo on the film plane in micrometres. Where they are much smaller it uses the model's exact pixel variance. Emulsion depth is calibrated to RMS granularity at a 48 um aperture. Then the spectral print: negative dyes with integral masking, a 3200 K printer lamp, the print stock's sensitivities and curves, timed by Newton's method so an 18% grey prints neutral at 18%, and bleach bypass as retained silver. Projection is under xenon, adapted to D65. |

Because grain lives on the film plane, the same negative shows larger, sharper grain at 4K and less at HD, as a real scan does. The node tests check Selwyn's law and the mean.

## Measurements

GPU parity against the CPU reference (8-bit, bounds committed before the first run in `evidence/shaders-*-parity-bounds.json`). On SwiftShader, every case has max difference 1, p99.9 at most 1, and mean at most 0.004 codes: 9 tube cases at 640 x 480 and 6 film cases at 480 x 270. Two first-run failures were fixed in the shaders, not the bounds:
- the delta mask's binary dot test flipped between f32 and f64 (max 57);
- a float Poisson threshold flipped one dye cloud (max 30).

Frame time, median wall per frame, SwiftShader (CPU WebGPU) in headless Chrome:

| Shader | Size | ms |
|---|---|---|
| tube, `pvm-20` (RGB) | 960 x 720 | 312 |
| tube, `slot-tv` (composite) | 960 x 720 | 492 |
| film, `500t-no-remjet` | 960 x 540 | 454 |
| film, `bleach-bypass` | 960 x 540 | 448 |

RTX 4090 frame times at 4K are in `evidence/shaders-timing-rtx4090.json` once measured.

## Limits, stated

- Most phosphor, mask, glass and film-stock constants are low confidence (see CONSTANTS.md). The structure is physical; several numbers are not yet measured. Data-sheet curves remain to be digitised.
- The print dye set was engineered by a grid search for a neutral grey scale, as real stocks are. It is not a digitised 2383.
- The halo kernel ignores the curvature's distortion of the kernel across the face.
- Grain in the small-cloud regime is white at pixel scale (its correlation length is below a pixel).
- The pigment spectra are parametric shapes, not measured pigments, and the painterly looks were tuned by eye on one test scene; the author's reference frames for template (a) will retune them.
- No comparison against existing shaders has run. The protocol is fixed in `evidence/shaders-comparison-protocol.json`. Until it runs, no claim of being better than any shader is made.
