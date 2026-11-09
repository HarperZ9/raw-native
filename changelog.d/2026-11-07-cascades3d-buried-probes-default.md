- **New default for the 3D cascades: buried probes found, traced fallback** (`web/shaders/cascades3d/math3d.mjs`,
  `cascades3d.mjs`, `world.mjs`, `wgsl3d-core.mjs`, `wgsl3d-passes.mjs`, `wgsl3d-world.mjs`, `gpu3d.mjs`).
  `planCascades3d` now also defaults to `pi 1` (a probe with at least half of 96 rays meeting a surface
  from behind counts as inside), `cg 6` (after the depth tests, traced segments to the probes in
  decreasing weight until one is reached) and `sk 1` (entries of buried probes are skipped). The CPU
  white furnace now holds to 1e-6 at 21 grids, where the old default read up to 15.7% low; the
  closed-room leak falls from 0.12-0.39% to 0.015-0.048%; raw-hall at 1080p reads 0.083 at 5.2 ms
  (0.082 at 4.9 ms before). Pass `pi: 0, cg: 2, sk: 0` for the old default.
