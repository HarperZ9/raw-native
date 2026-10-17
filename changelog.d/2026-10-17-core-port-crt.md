- **The physical CRT in the C++ core; N1 closes** (ADR 0019). The plan in physical units (presets,
  spectral phosphor screens, the faceplate halo kernel from the glass optics), the video signal
  through RGB or composite with a comb or notch decoder (NTSC and PAL), the beams' current-dependent
  spots integrated over each pixel's footprint on the curved face with the mask and phosphor
  persistence, and the halo, glass reflection and display encode run as seven `style.wgsl` passes.
  The C++ matches the JavaScript on all eleven cases (image within 6e-8, no code differs), and WARP
  is within 1 code. The no-third-party shader check now also reads the C++ style references
  (`--cpp`). With this, every style pass module and the look curves run in the core.
