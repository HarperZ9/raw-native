- **Path tracer interface v4: energy and watertightness** (`docs/rt-interface-v4.md`). The GPU and
  plain-scene lobe now conserves energy with Fresnel-weighted Lambert and Kulla-Conty multiple
  scattering: every white furnace case sits within 2.8e-3, where it reached 1.78 and 0.31.
  `PathTraceDesc::maxBounces` defaults to 64, which returns 0.997 of a closed box at albedo 0.8 against
  0.863, with a documented remaining bias above albedo 0.9. `intersectTri` decides hits by the
  watertight test of Woop, Benthin and Wald, so no ray through a shared edge leaks. Output of plain
  scenes changes, and `rr` reference frames rendered before v4 are biased.
