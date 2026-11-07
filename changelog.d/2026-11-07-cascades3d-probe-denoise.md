- **Probe denoise for the 3D radiance cascades** (`web/shaders/cascades3d/denoise3d.mjs`,
  `wgsl3d-denoise.mjs`). Opt-in temporal accumulation for the screen probes (per-frame probe jitter,
  reprojection, plane and normal history rejection) and an edge-aware a-trous filter over the probe
  grid, on CPU and GPU with parity cases. On raw-hall nave at 1080p against the path trace, relative
  RMSE went from 0.104 to 0.094 at 4.3 ms of GI. The fine probe speckle is gone. The band mottle fell
  only 19% against a 25% kill line, because the larger blotches come from the world levels, so the
  denoise stays off by default.
