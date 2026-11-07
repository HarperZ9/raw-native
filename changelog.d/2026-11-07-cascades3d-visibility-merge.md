- **Depth-moment merge weights at the 3D screen probes** (`web/shaders/cascades3d/world.mjs`).
  Opt-in `mw: "vis"` weighs level 1's neighbours by Chebyshev visibility from their depth moments
  instead of the on/off segment test. A null for the blotch (-2% wall mottle), at 0.28 ms less GI.
