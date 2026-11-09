- **Dropped-share ray switch for the 3D cascades, measured as a null** (`web/shaders/cascades3d/world.mjs`,
  `wgsl3d-passes.mjs`). Opt-in `fd: 1` (screen probes) or `fd: 2` (every merge): the share a merge drops
  to start visibility takes its own ray traced on. raw-hall's +1.3% bias stays (+1.36%, +1.51%) and
  the GI time rises to 7.5 to 8.2 ms, so it stays off.
