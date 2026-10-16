- **Denoiser inference: slimmer aux buffer** (HW H2). The v4 feature pass wrote 88 bytes a pixel of auxiliary data; it
  now writes 28 (the warped histories and the validity). The blend recomputes the demodulated radiance from the input and
  the combine reads the albedos and emission from it, with the same arithmetic: SwiftShader parity is identical on every
  test sequence. On the RTX 4090 the feature pass falls from 2.51 to 1.61 ms and the frame by 0.93 to 0.94 ms (three runs
  in each of two windows). The 2.0 ms budget still fails. Prediction and bounds: `evidence/rr-aux-slim-note-2026-10-15.json`.
