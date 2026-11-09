- **Single-segment cache fallback for the 3D cascades, measured as a null** (`web/shaders/cascades3d/cascades3d.mjs`,
  `wgsl3d-core.mjs`). Opt-in `cg: 5` traces one segment, to the probe of largest weight, and `cg: 4`
  traces to every eligible probe. `cg 5` leaves the CPU white furnace 1.2% low at one grid; `cg 4`
  holds it but costs 1.3 ms more than `cg 6` without `sk`.
