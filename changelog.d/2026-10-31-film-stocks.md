- **Film-stock library and physical-shader option libraries** (`web/shaders/film/library.mjs`,
  `web/shaders/options.mjs`, `crt/vector.mjs`, `lens/glasses.mjs`, `sensor.mjs`). Nine colour
  negatives with digitised data-sheet curves (VISION3 50D, 200T, 500T; PORTRA 160, 400, 800;
  EKTAR 100; GOLD 200; SUPERIA X-TRA 400) render through the 2383 print as `<stock>-print`
  presets, each with its own logistic curves, sensitivities, dye peaks and emulsion scatter where
  its sheet gives them, and a list of what it borrows from 500T. Reversal, black-and-white and
  print curves are library data. Halation control is `none`, `remjet` or `ahu`: Kodak's 3-26
  VISION3 editions replace the rem-jet with an antihalation undercoat, and the `cinestill-800t`
  and `cinestill-50d` presets are the rem-jet editions with it removed. CRT presets from cited
  mask records, the oxysulfide P22 red, and a vector (stroke) display mode; 1609 catalogue glasses,
  coatings and lens eras; 28 measured camera sensitivities. Every option record carries
  provenance and one without it is refused. Existing presets render unchanged (recorded
  fingerprints in `web/shaders/golden/`).
