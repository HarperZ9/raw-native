- **New default for the 3D cascades: leak-tight merges with grid jitter** (`web/shaders/cascades3d/math3d.mjs`).
  `planCascades3d` now defaults to `sv 2, ft 1, cg 2` and, with screen probes, `tf 32, jit 8` (jit 0
  when tf is 0). raw-hall at 1080p: relative RMSE 0.082 (0.077 to 0.088) against 0.104, GI 5.0 ms
  against 3.9 ms, and the closed-room leak under 0.4% at every grid placement tested, where the old
  default read up to 37%. Pass `sv: 0, ft: 0, cg: 0, tf: 0` for the old setting. Depth moments now
  take 16 bytes a texel instead of 8.
