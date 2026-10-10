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

## VHS (`web/shaders/vhs/`)

A videotape, simulated in the signal domain for template (c). Presets: `sp-fresh`, `lp-worn`, `ep-rental` (with a rolling tracking band) and `thriller` (a worn second-generation LP copy).

| Pass | What it simulates |
|---|---|
| `sample` | The line read off the tape, sampled at 4 fsc, with the transport's time-base error: a slow wander, line-to-line jitter, the head-switch skew in the bottom lines, and the mistracking band's shove. |
| `tape` | Luma through the FM channel: a windowed-sinc band limit at the tape speed's bandwidth (3.0 MHz SP, 2.5 LP, 2.2 EP), emphasis ringing (`peaking`), and noise that rises with frequency. Chroma through colour-under: a gaussian band limit at about 0.5 MHz, smooth like the analogue chain, a delay against luma, noise that streaks along the line, and a per-line phase wobble (hue). Generations narrow the bandwidths and add the noise. |
| `play` | Oxide dropouts, filled from the line above as a deck's dropout compensator does, then back to R'G'B'. |
| `show` | The signal lines at a display size. Or feed `signal` straight into the tube: the showcase plays the tape over composite into a Trinitron set. |

Measured on the CPU reference (`vhs.test.mjs`):
- 1 MHz luma detail passes and 4 MHz is mostly removed;
- the chroma edge rises more than three times as wide as the luma edge and lags it by the set delay;
- with every loss off, a flat field passes unchanged;
- dropouts occur at the set rate;
- the head switch moves only the bottom lines.

## Pixel art (`web/shaders/pixel/`)

3D pixel art that stays still while the camera turns, orbits and dollies, for template (b). The method follows Ebert's "Texel Splatting: Perspective-Stable 3D Pixel Art" (arXiv 2603.14587, CC BY 4.0), reimplemented from the paper. The paper's demo code has no stated licence and was not read.

| Pass | What it does |
|---|---|
| `capture` | A cubemap of the scene (N texels per face) from a probe at the camera position snapped to a world grid (`cell`). Each texel keeps its hit's Chebyshev distance, normal, material and object. |
| `shade` | Each texel shaded once, independently of the camera: posterised OKLab lightness (`bands`), and selective outlines, a darker shade of the object's own colour, where a texel borders a farther object (`outline`) or a crease (`crease`). |
| `splatz`, `splatid` | Every texel splatted as a world-space quad (corners at the texel's corner directions, at its depth, expanded by `expand`) into a visibility buffer. One pass finds the nearest depth key with atomics, the next the lowest texel index at that key. |
| `resolve` | The texel's colour, or an eye ray for pixels no texel reaches. On a cell change a 4 x 4 Bayer threshold crossfades from the previous probe. The blend advances each frame by the larger of 1 / `fadeFrames` and the distance moved over `fadeCells` of a cell. The paper leaves the timing open, so this rule is this library's choice. |

Within a cell, a texel keeps its colour as the view changes. Measured by reprojection (`pixel/stability.mjs`): on a small orbit, turn and dolly, 0.00% of compared pixels change colour with texel splatting, against 5.5%, 12.2% and 19.2% for naive pixelisation (the same shading rendered at low resolution and upscaled). At a cell change the probe moves, and texels shift. Slow cameras crossfade; fast ones switch within a frame.

Limits: the scene is a raymarched SDF diorama (`pixel/scene.mjs`, with a WGSL twin), because raw-native has no mesh rasteriser yet. Geometry the probe cannot see is filled by eye rays, which can shimmer. Outlines stop at cube-face seams.

`pixel/scale.mjs` covers roadmap S3. Integer upscaling is bit-equal to nearest neighbour on the GPU. Sharp-bilinear keeps texel interiors exact, blends one output pixel at each seam, and takes the subpixel camera offset of a snapped low-resolution render.

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

Everything textural (paper, canvas weave, bristles, stroke identity, warp) is noise at canvas coordinates, in one of two modes:

- **Pan offset** (`canvasOffset`): the pixel plus the camera's 2D pan. Exact for an isometric camera that pans. On a one-pixel pan the mean frame-to-frame difference is 0.000 to 0.007 codes, against 0.6 to 8.3 screen-locked.
- **Advected** (pass `{ mv, dist, distPrev }` from the renderer to `frame`): each pixel fetches its surface's canvas coordinate from last frame along the motion vector, after Neyret's "Advected Textures" (2003). It starts fresh where a depth test shows a disocclusion. Two layers carry the coordinates. A layer regenerates only when its mean distortion passes 0.35, measured as the departure of the local scale from 1 with rotation excluded and disocclusion seams left out. The new layer ramps in over 8 frames while the old one fades. A pure 2D pan never distorts, so it never regenerates. On the street scene at 160 x 100, the paper texture's reprojected change falls from 8.63 to 3.66 codes on an orbit, from 9.97 to 3.80 on a dolly, and from 3.82 to 2.99 on a parallax pan. In the full painting the orbit and dolly improve by 16 to 17%. The parallax pan is unchanged (2.30 against 2.33), because the image-driven parts dominate there. The residual comes from resampling fine noise every frame.

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

## Development adjacency (`web/shaders/adjacency/`)

Film edges as a darkroom makes them. While a negative develops, developer diffuses through the emulsion and is used up where the exposure is high, and the bromide that development releases diffuses too and holds development back. The pass integrates those two fields on a grid, coupled to the three layers' development. One rate law gives the Eberhard effect (a dense area develops more at its edge), Mackie lines (a dark rim on the thin side), the small-area effect (a small bright spot ends denser than a large one) and developer exhaustion in big bright areas. Presets: `eberhard`, `mackie`, `exhausted`. Lengths are film-plane micrometres; the chemistry grid coarsens itself so a frame takes about 34 steps at any size. The physics follows Rajkowski and Nowak (Optica Applicata, 2001); a dated literature check found no real-time rendering of it, so it is new to us and recorded as such.

Checks: a flat field stays flat; an edge overshoots on the dense side and undershoots on the thin side; with diffusion off nothing overshoots; the edge width scales with the square root of the diffusivity (ratio 1.85 for 4x, converging to 2 with resolution); developer and bromide are conserved to 1e-9 when the bath is cut off; a small spot develops more than a large one.

## Scan-causal EHT sag (`web/shaders/sag/`)

A tube's high voltage sags while the beam draws current and recovers with a time constant. Deflection goes as 1/sqrt(V), so the raster grows and the spot softens and dims. The pass walks the lines in scan order, so a line's geometry depends only on what was drawn before it: a bright window bows the lines below it and leaves the lines above alone. Output is R'G'B' signal for the tube's input. Presets: `consumer` (about 1% growth at full white), `thriller`. Shaders that model "raster bloom" scale the whole frame by its average brightness; this pass is causal within the frame.

Checks: zero current gives the identity raster; no line above a bright box moves (a line just below it does); a white field settles at 1 / sqrt(1 - S); recovery takes tau lines and twice as long at twice tau; line positions stay strictly increasing.

## Pigment-space lighting (`web/shaders/lightpaint/`)

Light decides what a painter would mix into the local colour. Each lit pixel's albedo becomes the paint's Kubelka-Munk latent; irradiance sets how much shadow pigment or light pigment is mixed into it. Yellow in shadow goes olive, red goes maroon, highlights take a warm or sickly white. The paint sets the chromaticity and the light keeps the luminance, so relighting keeps its energy. Presets: `disco` (violet-blue shadows, warm whites), `grotesque`, `complement` (each shadow takes the complement of its light). Inputs: albedo, irradiance, fog and pixel kind, as a G-buffer provides them. The nearest prior work (Lei and Chang, 2004) precomputes one hand-painted KM colour band per object; here the mix runs per pixel for any albedo.

Checks: at strength 0 the result is albedo times irradiance (to 1e-12); a yellow albedo in shadow turns 16 degrees toward green in Oklab, between the committed 5 and a stricter 40, while an RGB multiply keeps its hue; paint stays a reflectance in [0, 1].

## Interference glazes (`web/shaders/glaze/`)

Pearlescent paint inside the painterly model. Flakes of mica coated with a titanium-dioxide film reflect by thin-film interference (Airy, s and p, on the paint's 31 wavelengths); what they pass reaches the pixel's own KM body colour and returns, by Kubelka's two-layer formula. The colour slides toward blue as the surface turns away. Film thickness is a field on the surface's world position. Presets: `beetle`, `oil-slick`, `bruise`. Interference shows strongly over dark grounds and faintly over light ones, as real pearlescent paint does.

Checks: the normal-incidence peak sits at 4nd and the 45-degree peak at 4d sqrt(n^2 - sin^2) (within 5 nm, by search on the continuous function); zero coverage returns the albedo; zero thickness changes it by under 0.005; spectral reflectance never exceeds 1.

## Hysteresis quantisation (`web/shaders/hysteresis/`)

Bands and palette snapping that hold still. Posterised light and toon bands boil when a surface sits near a threshold and the light flickers or the camera drifts. Each pixel keeps the last frame's decision, found through the motion vectors, and changes it when the input leaves that decision's interval by a margin (in Oklab lightness), or when the plain decision has disagreed with it for 0.2 s. A different surface under the reprojection (by view distance) starts fresh. Presets: `bands6`, `pico8`.

Checks: zero margin and static input give the plain decisions exactly; under the street's flickering lamp, decisions toggle at 0.02 to 0.03 of the plain rate (bound 0.25), and a held-out sequence at 30 fps gives 0.044; a held band is never more than one band from the plain one; under a lamp that doubles, at least 0.9 of the plain band changes still happen; under a camera orbit, reprojected memory toggles less than plain.

## Measurements

GPU parity against the CPU reference (8-bit, bounds committed before the first run in `evidence/shaders-*-parity-bounds.json`). On SwiftShader, every case has max difference 1, p99.9 at most 1, and mean at most 0.004 codes: 9 tube cases at 640 x 480 and 6 film cases at 480 x 270. Two first-run failures were fixed in the shaders, not the bounds:
- the delta mask's binary dot test flipped between f32 and f64 (max 57);
- a float Poisson threshold flipped one dye cloud (max 30).

Lab 2 techniques, same method (bounds in `evidence/shaders-{adjacency,sag,lightpaint,glaze,hysteresis}-parity-bounds.json`, every run including failures in `evidence/shaders-*-runs.json`): every case max 1 code on SwiftShader; hysteresis decisions identical on 2,048,000 of 2,048,000.

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
- Lab 2 constants are mostly choices or low confidence: no measured adjacency diffusion length and no EHT source impedance or time constant were found, and the rutile index is recalled. The novelty records are dated web searches, not a full ACM DL and Scholar search, so the techniques are new to us until that is done.
- No comparison against existing shaders has run. The protocol is fixed in `evidence/shaders-comparison-protocol.json`. Until it runs, no claim of being better than any shader is made.
