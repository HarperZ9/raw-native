- **New default for the 3D cascades: screen rays traced to the end** (`web/shaders/cascades3d/math3d.mjs`,
  `cascades3d.mjs`, `wgsl3d-passes.mjs`, `gpu3d.mjs`). `planCascades3d` now defaults to `sx 1`: screen
  probes trace each ray to its hit and no longer merge level 1. The merge's parallax made round 7's
  +1.4% bias and the bright column-flank band. raw-hall at 1080p (stride 8, m 5) reads 0.054 at
  5.5 ms, bias +0.3%, against 0.083 at 5.1 ms; at 4K with stride 16, 0.073 at 7.1 ms against 0.094
  at 10.6 ms. Pass `sx: 0` for the old default.
