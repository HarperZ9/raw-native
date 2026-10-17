- **The spectral film shader and the lens suite in the C++ core** (ADR 0019). Film runs as a
  frame graph of expose, emulsion MTF, base halation, develop and a spectral print with
  Boolean-model grain; its image equals the JavaScript's on every f32 value of six cases, the
  Monte Carlo grain included, and WARP is within 1 code. The CIE 1931 tables are generated into
  `src/renderer/data/cie1931.hpp` and checked in CI. The lens suite ports the paraxial ghost
  prescription, the iris FFT starburst, the coating, streak and halation; the C++ matches to
  5.7e-16 and WARP is within 1 code. Three of the four source-edit controls failed as
  predicted; the lens halation edit stayed inside the web bound and is recorded as a miss.
