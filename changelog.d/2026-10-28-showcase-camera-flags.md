- **Camera and sun flags for showcase renders** (`rt-pathtrace`, `clouds`).
  `rt-pathtrace` takes `--eye`, `--target` and `--fovy` and draws every
  `rr-render` scene (`raw_hall`, `raw_hero`, `cloister`, `terrain_<seed>`, ...),
  so a caller can fly a camera path frame by frame on the GPU path tracer.
  `clouds --scene NAME` renders one scene with its sun, turbidity, camera,
  exposure and march step overridden. With no new flag both commands write the
  same bytes as before (checked on `retro_room` 960 x 540 x 256 spp on D3D12
  and on all five cloud goldens).
