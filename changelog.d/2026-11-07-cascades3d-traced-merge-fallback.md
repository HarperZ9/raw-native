- **Traced fallback for 3D cascade merges** (`web/shaders/cascades3d/cascades3d.mjs`, `world.mjs`,
  `wgsl3d-passes.mjs`). Opt-in `ft: 1`: a merge that trusts none of its eight neighbours traces its
  own ray on to the end of the scene, instead of averaging all eight (which leaks through thin walls)
  or returning no light (`fz`, which darkened raw-hall by 21%). Off by default.
