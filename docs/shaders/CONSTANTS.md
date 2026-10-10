# Shader library constants: sources and confidence

Each physical number the CRT and film shaders use, where it comes from, and how far to trust it. Confidence: **high** (a standard or a derivation), **moderate** (a secondary source that agrees with others), **low** (recalled, not yet checked against a primary source). Low-confidence numbers are inputs that can be changed, not claims. When a primary source is read, update the row and the code in the same commit.

## Colour (`web/shaders/spectral.mjs`, `cie1931.mjs`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| CIE 1931 2-degree observer | 380 to 780 nm, 5 nm | CIE 015:2018 tables | high |
| SMPTE C primaries | R (0.630, 0.340), G (0.310, 0.595), B (0.155, 0.070) | SMPTE RP 145; also listed on Wikipedia "NTSC" | high |
| EBU Tech 3213 primaries | R (0.640, 0.330), G (0.290, 0.600), B (0.150, 0.060) | EBU Tech 3213 | high |
| NTSC 1953 primaries, white C | R (0.67, 0.33), G (0.21, 0.71), B (0.14, 0.08); C (0.310, 0.316) | FCC 1953; Wikipedia "NTSC" | high |
| D65 | (0.3127, 0.3290) | CIE | high |
| Japanese 9300 K + 27 MPCD | (0.281, 0.311) | recalled | moderate |

## CRT (`web/shaders/crt/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Tube power law | gamma 2.4 | ITU-R BT.1886 (a model of the CRT) | high |
| P22 red | Y2O3:Eu line spectrum, main line 611 nm | Wikipedia "Phosphor" (P22R 611 nm) | moderate |
| P22 green | ZnS:Cu,Al band, peak 535 nm, FWHM 80 nm | peak from Wikipedia (530 nm); width recalled | moderate (peak), low (width) |
| P22 blue | ZnS:Ag band, peak 450 nm, FWHM 60 nm | peak from Wikipedia; width recalled | moderate (peak), low (width) |
| P22 model against standards | red 0.021 from EBU, green 0.005 from SMPTE C, blue 0.007 from EBU (xy distance) | measured by `crt.test.mjs` | measured |
| P4, P7, P31, P39, P1 peaks | P4 450 + 565 nm; P7 440 nm flash, 558 nm afterglow; P31 531 nm; P39 and P1 525 nm | Wikipedia "Phosphor" | moderate |
| Decay constants (all screens) | P22: red 0.35 ms; green 20 us + 0.5 ms tail; blue 5 us + 0.2 ms tail. P7 afterglow 30 ms + 0.4 s | recalled; fitted as two exponentials | low |
| Beam spot | sigma 0.16 to 0.34 line spacings (default), growing as current^0.5 | consumer spot 0.6 to 1.0 mm FWHM, exponent 0.4 to 0.7, recalled | low |
| Aperture grille pitch | 0.30 mm (PVM-class), 0.70 mm (consumer) | recalled | low |
| Damper wires | 2 wires at 1/3 and 2/3 height above 15 inches, 1 below | Wikipedia "Trinitron" | moderate |
| Slot mask pitch | 0.75 mm | recalled (0.6 to 0.8 mm) | low |
| Delta mask pitch | 0.28 mm | common 14-inch monitor figure | moderate |
| Faceplate | 9 to 13 mm, n = 1.52, transmission 0.46 to 0.85 | recalled | low (thickness, tint), moderate (index) |
| Halo ring radius | 2 t tan(asin(1/n)) | derived from total internal reflection | high (geometry) |
| Phosphor-layer albedo | 0.5 | assumed | low |
| Tube curvature | spherical R 650 to 900 mm; cylindrical R 1500 to 1700 mm | recalled (1.5 to 2.5 times the diagonal) | low |
| NTSC subcarrier | 315/88 = 3.579545 MHz | FCC | high |
| NTSC luma, I, Q bandwidth | 4.2, 1.3, 0.4 MHz | FCC; Wikipedia "NTSC" | high |
| PAL subcarrier | 4.43361875 MHz; U and V 1.3 MHz; luma 5.0 MHz | ITU-R BT.470 (recalled) | high (subcarrier), moderate (bandwidths) |
| Line phase | NTSC 180 degrees a line and a frame; PAL 283.7516 cycles a line | standards | high |
| Console pixel clocks | 5.369318 MHz (NES, SNES 256-wide, Genesis H32, PS1 256-wide); 6.712 MHz (Genesis H40, PS1 320-wide) | recalled from the master clocks | moderate |

## Film (`web/shaders/film/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Layer order | blue on top, yellow filter, green, red nearest the base | standard colour-negative construction | high |
| Negative layer sensitivities | gaussians at 645, 545, 445 nm, 55 to 60 nm FWHM | recalled shape of Kodak H-1 curves | low |
| Negative gamma, span | 0.62, 2.6 density | Kodak data sheets (about 0.55 to 0.65) | moderate |
| Negative dyes | cyan 670 nm, magenta 550 nm, yellow 450 nm, with unwanted absorptions (cyan 30% green and 8% blue; magenta 15% blue) | recalled | low |
| Integral masking | coloured couplers cancel the unwanted absorptions | photographic principle | high (principle) |
| Interimage gain | 0.3 | DIR couplers change gamma 10 to 30%, recalled | low |
| Print gamma, span | 2.8, 3.9 density | ASC (print gamma about 2.6); Kodak 2383 | moderate |
| Print dyes and red sensitivity | cyan 655, magenta 555, yellow 445 nm; red sensitivity 690 nm | chosen by grid search for a tracking grey scale (RMS chroma drift 18.7% to 2.8%) | engineered, not measured |
| Printer point | 0.025 log E (12 points = 1 stop) | lab practice | high |
| Printer lamp, projector | 3200 K and 5800 K blackbodies | xenon about 5400 to 6000 K, recalled | low |
| Film base | 0.125 mm, n = 1.49 (triacetate) | recalled | low (thickness), moderate (index) |
| Halation ring | 2 t tan(asin(1/n)) = 0.22 mm | derived | high (geometry) |
| Halation reach | red 0.25, green 0.06, blue 0 of the light reaching the base | inferred from layer order | low |
| Remjet | absorbs 97% of the halation | assumed | low |
| Pressure plate reflectance | 0.05, diffuse | matte black anodised metal, recalled | low |
| Emulsion scatter | gaussian sigma 4.2 um (50% MTF near 45 cycles/mm) | assumed figure for a modern camera negative | low |
| Dye clouds | radius 3.2 to 4.2 um (500T), 2.4 to 3.0 um (250D) | dye clouds 3 to 10 um, recalled | low |
| RMS granularity target | sigma D 0.008 (500T), 0.006 (250D) at 48 um, net density 1.0 | Kodak's measurement definition (high); the values are recalled | low |
| Selwyn's law | sigma * sqrt(A) constant | Selwyn 1935 | high |
| Boolean grain model | lambda(u) = ln(1/(1-u)) / (pi r^2) | Newson, Delon, Galerne, CGF 2017 | high |
| Super 35 gate | 24.89 mm wide | SMPTE 59 | high |
| Gate weave | 6 um drift, 1.5 um jitter | camera weave 5 to 10 um, recalled | low |

## Dither (`web/shaders/dither/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| OKLab matrices | Ottosson's, as in the superstack contract | Ottosson 2020; vendored reference | high |
| Bayer matrices 2, 4, 8 | standard recursive | Bayer 1973 | high |
| Interleaved gradient noise | 0.06711056, 0.00583715, 52.9829189 | Jimenez 2014 | high |
| Blue-noise mask | void-and-cluster, 64 x 64, sigma 1.5, 10% seed density, seed 20261010 | Ulichney 1993 (method) | high (method) |
| Pair penalty | 0.12 | the Studio's retro-dither.js | reference value |

## Paint (`web/shaders/paint/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Kubelka-Munk reflectance | R = 1 + K/S - sqrt((K/S)^2 + 2 K/S); thin layer by the hyperbolic solution | Kubelka and Munk 1931 | high |
| Pigment masstones | titanium white, hansa yellow, quinacridone magenta, phthalo blue as smooth spectral shapes | from the pigments' known colour, not measured | low |
| Relative scattering | white 1.0, yellow 0.35, magenta 0.18, blue 0.12 | assumed (white scatters most, organic pigments are transparent) | low |
| Latent lookup | 17^3, Levenberg-Marquardt in a cube-root colour space | this library | engineered |
| Kuwahara | 8 sectors, zeta 0.33, q 6 to 10, radius 5 to 7 px | Kyprianidis et al. 2009, 2010 (form); values chosen by eye | form high, values tuned |
| Advected canvas | regenerate at mean scale departure 0.35; 8-frame ramp; 12-frame minimum age; 2% depth test; seams (departure over 1) excluded | Neyret 2003 (method); thresholds are this library's choices | method high, values chosen |
| XDoG | k 1.6, tau 0.985, epsilon -0.004, phi 25 to 45 | Winnemoeller et al. 2012 (form); values chosen by eye | form high, values tuned |

## Pixel art (`web/shaders/pixel/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Probe position | camera snapped to a world grid; texels by cube direction | Ebert, arXiv 2603.14587 | high (method) |
| Probe resolution | 128 per face (CPU default), 192 in the showcase | the paper uses 384 | choice |
| Grid cell | 0.5 world units (about a twelfth of the diorama) | the paper gives none; chosen by eye: 2.0 smeared thin objects | choice |
| Bands, outline, crease | 6 bands; lightness shifts 0.22 and 0.08 | the paper gives none | choice |
| Quad expansion | 1.15 of a texel | the paper gives none | choice |
| Crossfade | 4 x 4 Bayer; blend step max(1/12, moved / (0.15 cell)) | Bayer from the paper; timing is this library's choice | choice |
| Depth key | view depth quantised to 1/4096 world unit, lowest texel index wins ties | this library (CPU and GPU agree on ties) | engineered |

## VHS (`web/shaders/vhs/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Luma bandwidth | 3.0 MHz SP, 2.5 LP, 2.2 EP (about 240 TV lines in SP) | VHS format, recalled | moderate |
| Chroma (colour-under) | about 0.5 MHz bandwidth around a 629 kHz carrier | VHS format, recalled | moderate |
| Chroma delay | 150 to 260 ns | typical of worn decks, recalled | low |
| Head switch | about 6 lines before vertical sync | VHS transports, recalled | moderate |
| Noise, jitter, dropout rates | per preset | chosen by eye | choice |
| Generations | bandwidth / sqrt(g), noise power x g | a simple model of re-recording | low |
