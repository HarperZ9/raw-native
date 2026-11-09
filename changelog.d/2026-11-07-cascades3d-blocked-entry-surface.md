- **Blocked-entry surface switch for the 3D cascades, measured as a null** (`web/shaders/cascades3d/cascades3d.mjs`,
  `wgsl3d-passes.mjs`). Opt-in `sd: 1` (with `sv: 2`): an entry whose skipped start is blocked holds
  the surface that blocks it instead of being dropped. It darkens raw-hall by 7% (RMSE 0.130), so it
  stays off.
