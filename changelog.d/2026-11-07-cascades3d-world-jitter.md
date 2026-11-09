- **World-grid jitter for the 3D radiance cascades** (`web/shaders/cascades3d/world.mjs`,
  `gpu3d.mjs`). Opt-in `jit: K` moves the whole probe grid by one of K offsets a frame and lets the
  temporal accumulation average them; each offset's depth moments and flags are traced once and
  reused. On raw-hall nave at 1080p it lowers the error against the path trace from 0.095 to 0.080
  and the wall mottle by 17% at 4.8 ms of GI, removing most of the world-level blotches. It stays off
  by default because it fails the thin-wall leak check (14% to 18% in the closed room).
