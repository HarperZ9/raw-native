- **Film-stock library and physical-shader option libraries** (`web/shaders/film/library.mjs`,
  `web/shaders/options.mjs`, `crt/vector.mjs`, `lens/glasses.mjs`, `sensor.mjs`). Twenty colour
  negatives with digitised data-sheet curves (VISION3 50D, 200T, 250D, 500T; PORTRA 160, 400, 800;
  EKTAR 100; GOLD 200; SUPERIA X-TRA 400; C200; PRO 400H; ETERNA 250, 250D, 400, 500, Vivid 160,
  Vivid 250D; REALA 500D; PHOENIX 200) render through the 2383 print as `<stock>-print`
  presets, each with its own logistic curves, sensitivities, dye peaks and emulsion scatter where
  its sheet gives them, and a list of what it borrows from 500T. Only reduced numbers ship; the
  digitised curves stay in a private table referenced by hash. Reversal, black-and-white (ILFORD, FOMA, Kodak, FUJIFILM) and
  print curves are library data. Halation control is `none`, `remjet` or `ahu`: Kodak's 3-26
  VISION3 editions replace the rem-jet with an antihalation undercoat, and the `cinestill-800t`
  and `cinestill-50d` presets are the rem-jet editions with it removed. CRT presets from manufacturer mask records, the oxysulfide P22 red, and a vector (stroke) display mode; 1609 catalogue glasses,
  coatings and lens eras; 28 camera matrices from measured sensitivities; multiple exposure and trichrome in `film/extras.mjs`. Every option record carries
  provenance and one without it is refused. 500t-print, 500t-no-remjet, 250d-print and
  bleach-bypass now read the digitised curves in JS, native and WebGPU, with the print dyes re-chosen for them; their
  goldens were re-recorded on purpose (before and after in `docs/shaders/CONSTANTS.md`). CRT and lens goldens did not move.
