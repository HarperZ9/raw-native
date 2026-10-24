- **Threads, rebuilt on our own method** (`threads.wgsl`). The shader no longer ports the One Step
  film's renderer. Threads are chains of eight nodes held on a form's level lines by position-based
  constraints, steered by a structure-tensor tangent field, paced by the rope ease and drawn as
  bilinear strokes with fibre and engraver shading; the finish is an asinh stretch with a power-law
  bloom and an iris on the spring token. Passes, bindings and parameters are unchanged. The advance
  pass costs about 1.2 to 1.6 times the old one. `web/threads.test.mjs` holds the interface and a
  structural difference from the ported code; `tests/gpu/threads_golden.py` holds six goldens.
