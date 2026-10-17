- **Palette dither and development adjacency in the C++ core** (ADR 0019). The palette dither
  (pair modes after retro-dither.js, interleaved gradient noise, blue noise and the mix modes)
  keeps its double-single arithmetic in the shared WGSL, and on WARP its palette index equals the
  float64 reference on every pixel of all 114 web cases, mix modes included. Development adjacency
  runs as a frame graph of prep, 30-odd Euler steps of development and chemistry, and output; the
  C++ matches the JavaScript to 5.6e-17 and WARP is within 1 code. All four source-edit controls
  failed as predicted. `style-parity --only <group>` runs one group while developing.
