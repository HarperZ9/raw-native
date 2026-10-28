- **4K frames on the D3D12 path tracer and ray marcher** (`rt_pt_gpu.cpp`,
  `sdf_gpu.cpp`). The path tracer's samples-a-submission slice now shrinks
  with the frame above 1280 x 720: at 3840 x 2160 the fixed 16-sample slice
  ran past the Windows GPU watchdog on `raw_hall` (about 3.2 s on an RTX 4090)
  and the device was removed. Output bytes are unchanged (`retro_room`
  3840 x 2160 x 32 spp, radiance and variance identical). The ray marcher's
  shading pass runs in slices of at most 65,535 groups, so `sdf-render` at 4K
  draws `garden` instead of writing a blank sky; `sdf-render` now reports a
  failed shading pass instead of writing that image. Still open: `menger_tower`
  at 3840 x 2160 removes the device (one submission for its march).
