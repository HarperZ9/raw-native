- **Own-ray start visibility for the 3D cascades** (`web/shaders/cascades3d/cascades3d.mjs`,
  `wgsl3d-world.mjs`). Opt-in `sv: 2` weighs each level entry 1 or 0 by whether its own texel-centre
  ray crosses the skipped start of its interval, where `sv: 1` used a share of 4 sub-texel rays that
  let entries whose ray passed through a wall count at up to 3/4. With `ft` it keeps the closed test
  room under 0.15% at nine grid placements; it raises raw-hall's error from 0.104 to 0.112, so it is
  off by default.
