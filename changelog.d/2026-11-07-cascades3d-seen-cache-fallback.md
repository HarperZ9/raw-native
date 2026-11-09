- **Segment-tested cache fallback for the 3D cascades** (`web/shaders/cascades3d/cascades3d.mjs`,
  `wgsl3d-core.mjs`). Opt-in `cg: 1`: when no level-0 probe passes the cache gather's depth test, only
  probes an unblocked segment from the surface reaches count. With grid jitter it halves the
  remaining leak (0.71% to 0.40% worst) at about 1.5 ms, which puts jitter over the 6 ms budget; off
  by default.
