- **Gallery gold highlight on lavapipe** (`src/renderer/gpu/shaders/pbr.wgsl`). The
  material tables' theta axis now reads through `acos_table`, a polynomial acos
  (Abramowitz and Stegun 4.4.46, error at most 2e-8), in place of the built-in acos.
  llvmpipe's acos was off by up to 1.6e-4 rad, which tilted the image-lighting lookup
  direction enough to move the gold row's near-mirror sun highlight by 1%. The
  gallery's worst exposed error on lavapipe falls from 0.118 to 0.0012 (bound 0.05),
  and the m3-scenes gate checks that bound again.
