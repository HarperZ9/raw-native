- **Sky model and the engine's own environment maps** (`raw/renderer/sky.hpp`,
  `raw_native_cli envmaps`, `raw_native_cli relight`). A Preetham clear and hazy sky
  (checked against the CIE standard clear sky) and the CIE overcast sky, baked into clear
  noon, overcast, golden hour and night-with-moon cubes, plus an interior probe path-traced
  inside the retro room. Every map is generated, never downloaded, and recorded with its
  recipe and hash in `evidence/envmaps.lock.json`. `relight` shows the material gallery and
  the sphere field under each map with their G-buffer channels.
