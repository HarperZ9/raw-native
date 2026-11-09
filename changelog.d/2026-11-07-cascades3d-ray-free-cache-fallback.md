- **Ray-free cache fallback for the 3D cascades** (`web/shaders/cascades3d/world.mjs`,
  `cascades3d.mjs`, `wgsl3d-core.mjs`, `wgsl3d-passes.mjs`). `cg: 2` (and the stricter `cg: 3`) tests
  the cache gather's fallback probes with the farthest (nearest) of the depth pass's 4 rays a texel,
  which the depth pass now keeps, instead of tracing a segment to each probe. In raw-hall at 1080p
  it saves 1.25 ms against `cg: 1` with the same leak (under 0.4%) and error (0.082).
