- **Level-1 angular resolution option for the 3D cascades** (`web/shaders/cascades3d/math3d.mjs`).
  Opt-in `c1` sets level 1's cube-map face side (96 directions at 4, from 24); a parent texel now
  splits into any power-of-two square of children. Measured as a null for the world-level blotch
  (-4% wall mottle) at 1.8 ms more GI.
