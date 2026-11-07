- **Radiance cascades in flatland** (`web/shaders/cascades`, `docs/radiance-cascades.md`). 2D global
  illumination after Sannikov's paper, on the CPU (float64) and in WebGPU, with branch 2 or 4, a
  per-neighbour continuation that stops thin-wall leaks, and bent-ray merging through analytic glass.
  Measured against a 4096-ray brute force at 3840 x 2160 on an RTX 4090: relative RMSE 0.022 on the
  planted disk and 0.038 on the room at branch 4 (34 and 49 ms), 0.11 and 0.12 at branch 2 (4.6 and
  12 ms). Within 2 px of a glass interface the error is 2.7 to 5.4 times the image's, so cascades
  alone do not carry refraction.
