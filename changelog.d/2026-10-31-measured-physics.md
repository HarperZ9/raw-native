- **Measured physics in the CRT, film and lens shaders** (`web/shaders/crt`, `film`, `lens`,
  and their C++ ports). The P22 decays are two-exponential fits to Kuhn 2002's measured
  curves (`crt/kuhn2002.mjs`; largest error in the cumulative light 0.004 red, 0.053 green,
  0.064 blue), replacing recalled constants. The 500T negative takes its layer sensitivities,
  dye peaks, per-layer gammas (0.45, 0.53, 0.51), per-layer granularity and per-layer
  emulsion scatter (50% MTF at 56.3, 49.9, 33.8 cycles/mm) from Kodak H-1-5219t; the emulsion
  blur now runs per layer on the CPU and GPU paths. The lens glasses are SCHOTT N-BK7 and
  N-SF2 by Sellmeier. The engineered print tracks the measured negative's grey scale at 3.7%
  RMS chroma (2.4% before); the print's own curves are not yet digitised. Film and CRT output
  changes.
