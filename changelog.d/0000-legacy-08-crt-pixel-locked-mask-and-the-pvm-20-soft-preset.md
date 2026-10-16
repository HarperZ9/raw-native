- **CRT: pixel-locked mask and the `pvm-20-soft` preset** (shader lab 4, #122). The stripe or slot
  mask can lock to the output pixel grid (`mask.lock: "pixel"`, `mask.pitchPx`), on the CPU
  reference and in WGSL. Every CRT parity case is within one code on SwiftShader. Merged just
  before the 0.8.0 tag was cut and not in 0.8.0.
