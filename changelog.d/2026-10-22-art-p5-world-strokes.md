- **The art track's base and P5, world-anchored strokes** (`raw_native_cli art strokes`). An
  owned analytic still life with true motion vectors, the rr-bounds warp metric with a paired
  bootstrap, and an art WGSL module with a pass runner. P5 fed the painterly shader world-anchored
  canvas coordinates in place of the advected canvas: GPU parity holds on WARP, lavapipe and the
  RTX 4090 and the anchor costs 0.11 ms at 1080p, but on a 48-frame orbit it is no steadier than
  the advected canvas (oil 14% worse, watercolour 19% better, neither interval clear of zero), so
  P5 is dropped by its own kill criterion.
