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
| P22 red | Y2O3:Eu line spectrum, main line 611 nm; one of the two P22 red hosts (Y2O2S:Eu, main line 626 nm, is the other, and the EIA triples differ) | Wikipedia "Phosphor" (P22R 611 nm); Kuhn 2002 notes the P22 red varies by triple | moderate |
| P22 green | ZnS:Cu,Al band, peak 535 nm, FWHM 80 nm | peak from Wikipedia (530 nm); width recalled | moderate (peak), low (width) |
| P22 blue | ZnS:Ag band, peak 450 nm, FWHM 60 nm | peak from Wikipedia; width recalled | moderate (peak), low (width) |
| P22 model against standards | red 0.021 from EBU, green 0.005 from SMPTE C, blue 0.007 from EBU (xy distance) | measured by `crt.test.mjs` | measured |
| P4, P7, P31, P39, P1 peaks | P4 450 + 565 nm; P7 440 nm flash, 558 nm afterglow; P31 531 nm; P39 and P1 525 nm | Wikipedia "Phosphor" | moderate |
| P22 decay | two-exponential fits [w, tau1, tau2] to the measured curves: red [0.077, 35.9 us, 0.427 ms], green [0.505, 25.4 us, 1.459 ms], blue [0.500, 14.4 us, 1.138 ms]; largest error in the cumulative light 0.004, 0.053, 0.064 from 1 us to 10 ms | Kuhn 2002, IEEE S&P, eqs. 8 to 10 (`crt/kuhn2002.mjs`): red six exponentials, slowest 360 Hz (tau 0.442 ms); green and blue power laws n 1.10 and 1.11, t0 5.5 and 5 us, plus exponentials, cut at 10 ms, the measured span. RCA TPM-1508A (1961) 10% times 38 us (ZnS:Cu) and 34 us (ZnS:Ag) agree with the power laws within 3% | moderate (one measured CRT, a second CRT source for green and blue) |
| Decay constants (other screens) | P7 afterglow 30 ms + 0.4 s; P4, P31, P39, P1 per screen | recalled; fitted as two exponentials | low |
| Beam spot | sigma 0.16 to 0.34 line spacings (default), growing as current^0.5 | consumer spot 0.6 to 1.0 mm FWHM, exponent 0.4 to 0.7, recalled | low |
| Aperture grille pitch | 0.30 mm (PVM-class), 0.70 mm (consumer) | recalled | low |
| Damper wires | 2 wires at 1/3 and 2/3 height above 15 inches, 1 below | Wikipedia "Trinitron" | moderate |
| Slot mask pitch | 0.75 mm | recalled (0.6 to 0.8 mm) | low |
| Delta mask pitch | 0.28 mm | common 14-inch monitor figure | moderate |
| pvm-20-soft locked mask | 3 output pixels per triad, mix 0.5 | chosen by looking at 4 variants (mix 0.35 to 0.6) against the panel crops, 2026-10-14 | choice |
| pvm-20-soft spot and bandwidth | sigma 0.25 to 0.42 line spacings, RGB 3.5 MHz | same comparison; consumer spot range 0.17 to 0.29 is the research figure, so this is wider than a sharp tube | choice |
| Faceplate | 9 to 13 mm, n = 1.52, transmission 0.46 to 0.85 | recalled | low (thickness, tint), moderate (index) |
| Halo ring radius | 2 t tan(asin(1/n)) | derived from total internal reflection | high (geometry) |
| Phosphor-layer albedo | 0.5 | assumed | low |
| Tube curvature | spherical R 650 to 900 mm; cylindrical R 1500 to 1700 mm | recalled (1.5 to 2.5 times the diagonal) | low |
| NTSC subcarrier | 315/88 = 3.579545 MHz | FCC | high |
| NTSC luma, I, Q bandwidth | 4.2, 1.3, 0.4 MHz | FCC; Wikipedia "NTSC" | high |
| PAL subcarrier | 4.43361875 MHz; U and V 1.3 MHz; luma 5.0 MHz | subcarrier: ITU-R BT.470; U and V: ITU-R BT.1700 (under 3 dB down at 1.3 MHz); luma 5 MHz is the B/G figure of BT.1701, which is not held | high (subcarrier), high (U, V), moderate (luma, not checked against a held primary) |
| Line phase | NTSC 180 degrees a line and a frame; PAL 283.7516 cycles a line | standards | high |
| Console pixel clocks | 5.369318 MHz (NES, SNES 256-wide, Genesis H32, PS1 256-wide); 6.712 MHz (Genesis H40, PS1 320-wide) | recalled from the master clocks | moderate |

### CRT options (`web/shaders/options.mjs`, `crt/vector.mjs`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Record presets | pvm-14l2 0.25 mm, pvm-20l2 0.4 mm, pvm-9l2 0.5 mm grille (Sony brochure, 2002); tm32-slot 0.75 mm slot-shadow (Ikegami data sheet); hitachi-cm81x 0.22 mm horizontal dot pitch Invar shadow mask (Hitachi manual); diamondtron-2070sb 0.24 mm grille (NEC-Mitsubishi brochure) | manufacturer documents, held with sha256 | high |
| Missing mask records | PVM-14M2U, PVM-20M4U, PVM-20L5, KV-27FS120, AV-32D201, SuperScan 814: no manufacturer document found states the pitch; no number kept | listed with the reason | not applicable |
| P22 red hosts | Y2O3:Eu, main line 611 nm (screen p22); Y2O2S:Eu, 626 nm with 616 nm (screen p22-y2o2s); YVO4:Eu (data only) | recalled line positions and heights | moderate (positions), low (heights) |
| Tube classes | consumer, PVM, BVM, computer monitor | consumer and BVM ranges recalled; PVM and monitor from the manufacturer records | low to high |
| Vector mode | slew 2 mm/us, settle 2 us at each end, spot sigma 0.35 mm | Atari QuadraScan and Wells-Gardner 6100 class displays, recalled orders of magnitude | low (inputs, not claims) |

### Camera sensors (`web/shaders/sensor.mjs`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Camera matrices | 28 cameras: a 3 x 3 from linear Rec.709 to camera RGB, integrated from relative curves at 400 to 720 nm, 10 nm. The curves stay private: their source states no licence | Jiang, Liu, Gu, Susstrunk 2013 (camspec database), cited | high (measurement), relative only |

## Film (`web/shaders/film/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Layer order | blue on top, yellow filter, green, red nearest the base | standard colour-negative construction | high |
| Negative layer sensitivities (500T) | peaks 650.2, 547.9, 401.8 nm; half maximum 599.1 to 665.2, 522.8 to 574.9, 380.6 to 475.5 nm, drawn as two-sided gaussians | Kodak H-1-5219t (2015), spectral sensitivity figure, digitised from its vector paths | moderate (one data sheet, digitised) |
| Negative layer sensitivities (250D) | gaussians at 645, 545, 445 nm, 55 to 60 nm FWHM | recalled shape of Kodak H-1 curves; its data sheet is not held | low |
| Negative gamma, span | 500T: 0.45 R, 0.53 G, 0.51 B over log H -2 to 0 (3.7% higher on the camera-stop axis); 250D: 0.62. Span 2.6 density | 500T: H-1-5219t characteristic curves, digitised; 250D: Kodak data sheets (about 0.55 to 0.65), recalled | moderate |
| Negative dyes | 500T peaks: cyan 686 nm, magenta 539 nm, yellow 447 nm (250D: 670, 550, 450 nm); widths and unwanted absorptions (cyan 30% green and 8% blue; magenta 15% blue) recalled | 500T peaks: H-1-5219t spectral dye density figure, digitised; the rest recalled | moderate (peaks), low (widths, absorptions) |
| Integral masking | coloured couplers cancel the unwanted absorptions | photographic principle | high (principle) |
| Interimage gain | 0.3 | DIR couplers change gamma 10 to 30%, recalled | low |
| Print gamma, span | 2.8, 3.9 density | ASC (print gamma about 2.6); Kodak 2383 | moderate |
| Print dyes and red sensitivity | cyan 655, magenta 555, yellow 445 nm; red sensitivity 690 nm | chosen by grid search for a tracking grey scale (RMS chroma drift 18.7% to 2.8% against the recalled negative). Against the measured 500T the same grid's best is 3.6%, and this choice gives 3.7%; 250D gives 2.4% | engineered, not measured |
| Printer point | 0.025 log E (12 points = 1 stop) | lab practice | high |
| Printer lamp, projector | 3200 K and 5800 K blackbodies | xenon about 5400 to 6000 K, recalled | low |
| Film base | camera negative: 0.125 mm, n = 1.49 (acetate with rem-jet); print 2383: 0.120 mm ESTAR (PET), n = 1.573 at 550 nm (unoriented PET; oriented ESTAR is birefringent) | negative: H-1-5219t names an acetate base with rem-jet but prints no thickness, so 0.125 mm is recalled; print: Kodak H-1-2383 (2022), PET index from Zhang (refractiveindex.info, CC0). Only the negative's halation is drawn | low (negative thickness), moderate (negative index), high (print thickness) |
| Halation ring | 2 t tan(asin(1/n)) = 0.22 mm | derived | high (geometry) |
| Halation reach | red 0.25, green 0.06, blue 0 of the light reaching the base | inferred from layer order | low |
| Remjet | absorbs 97% of the halation | assumed | low |
| Pressure plate reflectance | 0.05, diffuse | matte black anodised metal, recalled | low |
| Emulsion scatter | gaussian per layer, sigma 3.33, 3.75, 5.54 um: 50% MTF at 56.3 (R), 49.9 (G), 33.8 (B) cycles/mm | Kodak H-1-5219t MTF figure, digitised; sigma = sqrt(ln 2 / 2) / (pi f50) | moderate |
| Dye clouds | radius 3.2 to 4.2 um (500T), 2.4 to 3.0 um (250D) | dye clouds 3 to 10 um, recalled | low |
| RMS granularity target | 500T: sigma D 0.0064 R, 0.0071 G, 0.0191 B; 250D: 0.006 each; at 48 um, net density 1.0 | Kodak's measurement definition (high); 500T read from the H-1-5219t granularity figure (Kodak notes its own modified measuring technique); 250D recalled | moderate (500T), low (250D) |
| Selwyn's law | sigma * sqrt(A) constant | Selwyn 1935 | high |
| Boolean grain model | lambda(u) = ln(1/(1-u)) / (pi r^2) | Newson, Delon, Galerne, CGF 2017 | high |
| Super 35 gate | 24.89 mm wide | SMPTE 59 | high |
| Gate weave | 6 um drift, 1.5 um jitter | camera weave 5 to 10 um, recalled | low |

### Film-stock library (`web/shaders/film/library.mjs`, `library-data.mjs`)

Generated by `tools/shaders/film_library.mjs` from the private measured film-stock table, which
digitises manufacturer data sheets: vector figures by their drawing paths with axes fitted through the
printed tick labels, sheets with outlined labels by their paths with axes hand-entered and checked against
the drawn gridlines, image figures by tracing strokes column by column. Only the reduced numbers (logistic
fit, peaks, half widths, 50% MTF frequency) travel in this repo, with each stock's file, sha256, page and
method and the table's own sha256. With `MEASURED_FILM_STOCKS` pointing at that table, `library.test.mjs`
checks every stock's table against points taken straight from the drawn curve (0.02 D for vector figures,
0.06 D for traced ones) and its logistic fit against its recorded worst residual; without it the test says
why it skipped, and colour negatives' recorded residuals stay bounded at 0.125 D.

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Colour negatives the shader renders | VISION3 50D, 200T, 250D, 500T; PORTRA 160, 400, 800; EKTAR 100; GOLD 200; SUPERIA X-TRA 400; C200; PRO 400H; ETERNA 250, 250D, 400, 500, Vivid 160, Vivid 250D; REALA 500D; PHOENIX 200 | Kodak, Kodak Alaris, FUJIFILM and HARMAN sheets; the discontinued FUJIFILM and the HARMAN sheets from Internet Archive captures of the maker's own URLs | moderate (digitised; logistic residual 0.02 to 0.12 D) |
| Named presets | 500t-print, 500t-no-remjet and bleach-bypass read VISION3 500T's digitised curves; 250d-print reads VISION3 250D's (own curves and spectral sensitivity, 500T's dye peaks, recalled grain). Emulsion scatter stays the first 500T reading on all four | H-1-5219t, H-1-5207 | moderate |
| Library-only stocks | EKTACHROME E100 and 100D, Velvia 50 and 100, PROVIA 100F (spectral only), TRI-X 7266, DOUBLE-X, T-MAX 100, 400, P3200 (spectral and MTF), TRI-X 400, ACROS II, ILFORD HP5, FP4, PAN F, SFX, XP2, DELTA 100, 400, 3200, ORTHO, FOMAPAN 100, 200, 400, Cine 400, ORTHO 400, RETROPAN 320; 2383 and VISION Premier 2393 print curves | same method; 2393 from a public copy of Kodak's H-1-2393t | moderate; the shader renders colour negatives only |
| 18% grey reference | printed Log H Ref where the sheet gives one, else log10(14.4 / ISO) | the five Kodak Alaris references follow log10(14.4 / ISO) within 0.006; on the 250D sheet stop 0 reads -1.3 against -1.24 | high (relation), moderate (applied to other makers) |
| Camera-stop and relative axes | ETERNA prints camera stops: stop 0 placed at the reference above. PHOENIX prints relative log exposure: placed by the ISO speed point, green at D-min + 0.15 at log10(sqrt(2) / 200). ILFORD and FOMA stay relative and are not rendered | derived | moderate |
| Borrowed numbers | dye masking and interimage for every library negative; dye spectra where only a neutral is published; spectral sensitivity for 200T; grain size | 500T record; each library negative lists its borrowed parts | low |
| 500T 50% MTF, second reading | 60.5 R, 52.8 G, 34.5 B cycles/mm | the same H-1-5219t figure with the axes snapped to the gridlines (the first reading placed the tick labels by their text boxes, about 1 pt off on the response axis); the 500t presets keep the first reading, the library stock uses the second | moderate |
| Measured 2383 curve | slope and span per layer from H-1-2383 (8-26, image figure); toe below D 0.17 unresolved | Kodak | moderate; with the engineered print dyes it tracks grey worse (11 to 26% RMS chroma) than the engineered curve (4 to 13%): the dyes and sensitivities must follow |
| Grey through the print | RMS chroma from -5 to +5 stops: Kodak motion-picture negatives 4.2 to 6.3%, others 5.4 to 12.6%, PHOENIX 24% (it states no masking dyes and the masking here is 500T's) | measured from the code | a limit of the borrowed masking |
| Halation control | none 1, rem-jet 0.03, antihalation undercoat (10^-0.6)^2 = 0.063 of the base return | rem-jet value as before; the undercoat's optical density is recalled | low |
| VISION3 backing | the 3-26 editions of H-1-5203, -5207, -5213, -5219 state that an antihalation undercoat replaces the rem-jet; the earlier editions state rem-jet backing | Kodak (both editions held) | high |
| PHOENIX halation | "limited antihalation" in the sheet with no density: treated as none, the upper bound | HARMAN | low |
| CineStill 800T, 50D | Kodak 5219 and 5203 with the rem-jet removed (halation none); rated EI 800 and 50 | CineStill's product text: no remjet layer, halation from its removal; the base stocks are third-party reporting (CineStill names none) | high (no rem-jet), moderate (stock identity) |
| Missing | Every listed stock has some data. Held, not digitised: Kodak 2302 (curves in dashed image lines), P3200 curves, Fomapan R100 (no curve printed). Captured: KENTMERE 100 and 400 print no characteristic curve; ETERNA Vivid 500 is image-only and not digitised | listed with the reason | not applicable |
| Print 2383 dyes | cyan 650 / 140 nm, magenta 555 / 95, yellow 440 / 90, red sensitivity 680: grid search of 4860 candidates against the digitised 500T and 250D curves, keeping interimage 0.3 (interimage 0 tracked grey better at 2.4% and 1.8% but lost colour: mean error 36 against 31) | engineered | defined |
| Golden changes 2026-10-31 (grey RMS % from -5 to +5 stops; mean CIE76 error of 12 fixed hues) | 500t-print and 500t-no-remjet 3.68 to 3.27, error 33.19 to 31.09; 250d-print 2.40 to 2.48, error 26.75 to 26.59; bleach-bypass 3.43 to 5.50, error 41.01 to 39.12. Bounds unchanged: 4% for 500T, 3% for 250D; bleach bypass has no such bound | measured from the code | the colour error compares with the scene and includes the look |
| Foma curves | FOMAPAN 100 and 400 print the same curve figure (gamma 0.74, 0.93, 1.11 on both) | observed in both sheets; recorded as printed | moderate |
| Multiple exposure, trichrome (`film/extras.mjs`) | linear addition before the negative; trichrome filters idealised to Rec.709 channels | exposure is additive within reciprocity; status defined for the filters | high (addition), low (filters) |
| Candidate options not built | redscale, base-side light leak, Aerochrome-style infrared, hue-swap stocks, film ageing, wet-plate collodion | each names its missing data in `film/extras.mjs` CANDIDATES | not applicable |

Native (`src/renderer/style_film_stocks.cpp`) carries the same per-layer slope, span and position for neg-500t and
neg-250d, generated by `tools/port/gen_film_native.mjs`; other library stocks are JS and WebGPU only. The WGSL and HLSL
passes already read per-layer `negK` and `negX0` from the plan, so no shader source changed.

## Lens (`web/shaders/lens/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Lens glasses | Sellmeier: crown N-BK7 (nd 1.5168, vd 64.17), flint N-SF2 (nd 1.64769, vd 33.82); before 2026-10-31, round Cauchy values near them | SCHOTT optical glass overview (2025); the Sellmeier gives vd 64.167 against the catalogue's 64.17 | high |
| Catalogue glasses (`lens/glasses.mjs`) | 1609 glasses from SCHOTT, OHARA, HOYA, CDGM, SUMITA, HIKARI; dispersion formulas 1 to 3; every printed nd reproduced to 2e-4 | refractiveindex.info release 2026-05-24 (CC0), specs/ | high |
| Lens eras | vintage: uncoated N-K5 / N-F2; coated: MgF2 quarter wave on N-BK7 / N-SF2; modern: MgF2 on S-FPL51 / S-NBH5 | design choices over catalogue data; the ghost prescription keeps its own glasses; multi-layer coatings are data only | defined |
| MgF2 index | 1.3785 at 550 nm | Dodge 1984, ordinary ray (RII, CC0) | high |

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

## Development adjacency (`web/shaders/adjacency/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Model | developer and bromide diffusion with consumption and restraint | Rajkowski and Nowak, Optica Applicata 31(2), 2001; Nelson's chemical spread functions | moderate (physics) |
| Diffusion lengths | 0.3 to 0.8 mm by preset | Eberhard effect reported within about 1 mm (secondary source); no measured length found | low |
| Rates (k, eta, r, beta, etaB, rB) | per preset, in units of the development time | chosen for the look | choice |
| Frame width on film | 24.9 mm (Super 35) | standard | high |
| Chemistry grid | longer length spans about 2.5 cells | engineered for a fixed step count | engineered |

## Scan-causal EHT sag (`web/shaders/sag/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Deflection gain | 1 / sqrt(1 - v), v the relative anode-voltage sag | magnetic deflection in 1/sqrt(Va), US 4297618 | moderate |
| Sag at full white, S | 0.02 consumer, 0.08 thriller | estimate from about 1 MOhm, 25 kV, 1 mA; no published figure found | low |
| Time constant | 0.1 and 0.05 of a frame | not found | choice |
| Blanking | 8% of the lines | NTSC and PAL blank about 8% of lines | moderate |
| Focus and brightness | sigma0 + focus v; (1 - v)^bright | chosen | choice |

## Pigment-space lighting (`web/shaders/lightpaint/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Pigments | the painterly shader's four parametric pigments | see Paint | low |
| Shadow and light mixtures, strengths, pivots | per preset | chosen by eye; disco tuned so yellow in shadow lands on olive | choice |

## Interference glazes (`web/shaders/glaze/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Film index | Cauchy 2.2 + 0.08 um^2 / lambda^2 (about 2.46 at 550 nm) | rutile-like, recalled | low |
| Binder and mica indices | 1.5 and 1.58 | typical, recalled | moderate |
| Thickness, coverage, noise scale | per preset | chosen | choice |

## Hysteresis quantisation (`web/shaders/hysteresis/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Band margin | 0.3 band | chosen; must stay under 0.5 for the one-band lag bound | choice |
| Palette margin | 0.03 Oklab | chosen | choice |
| Persistence | 0.2 s | swept on the 24 fps test sequences, checked on a held-out 30 fps sequence | engineered |
| Disocclusion | view distance within 3% | chosen | choice |

## CRT purity and degauss (`web/shaders/purity/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Stripe and beam widths | 0.26 and 0.2 of the triad pitch | chosen so a centred beam lands inside its stripe | choice |
| Per-gun spread | outer guns land with (1 +/- 0.35) of the error | chosen so white tints; no source | low |
| Degauss frequency | mains, 60 Hz on a 59.94 Hz field | degauss current is at mains (US 5901029) | high (mains), moderate (60 Hz region) |
| Degauss envelope | exponential, tau 0.25 s (gone by about 1.25 s) | coils run 1 to 4 s (US 6686695); a thermistor envelope, not measured | low |
| Landing errors | 0.015 to 0.16 of the pitch by preset | landing variation 23 to 131 um by tube size (US 6686695) | choice |

## Fibre-network watercolour (`web/shaders/fibre/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Fibre process | fixed count per cell, uniform centres, angles about a machine direction | Kallmes-Corte random line process (via Sampson and Dodson), stratified | moderate (model), choice (form) |
| Fibre length, width | 0.05 to 0.08 of the image height; 1 to 1.4 px | not found for watercolour paper | choice |
| Conductivity | k0 0.2 to 0.3, k1 0.5 to 0.95 (k1 <= 4.8 k0 keeps the stencil monotone) | chosen | choice |
| Deposition, flocculation, evaporation | per preset | chosen by eye | choice |

## Curvature hatching (`web/shaders/hatch/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Finite-difference step | 0.01 scene units | error about (E / R)^2 on the still life's radii | engineered |
| Umbilic threshold | curvature gap under 0.12 of the larger curvature | chosen | choice |
| Line density | 22 to 34 lines per metre, nested to keep about 4 px | chosen | choice |

## Salience caricature warp (`web/shaders/caricature/`)

| Constant | Value | Source | Confidence |
|---|---|---|---|
| Fold bound | det J >= 0.04 (clamp 0.8 for one step; 1 - 0.04^(1 / (2 steps)) per step) | derived | exact |
| Salience weights, blur | per preset (head 1.0, hat 0.6 to 0.7, coat 0.45 to 0.5, lamp 0.4 to 0.5) | chosen | choice |
