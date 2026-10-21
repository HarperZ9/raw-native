- **Path tracer interface v3** (`raw/renderer/rt_pathtrace.hpp`, `docs/rt-interface-v3.md`).
  The CPU path tracer takes punctual lights in physical units, an environment cube
  (importance-sampled and MIS-weighted on escape) and the full glTF material model:
  clearcoat, sheen, anisotropy, iridescence, and thin and volume transmission with
  traced Beer-Lambert absorption. Every v2 scene renders byte for byte as before
  (hashes pinned for GCC and MSVC); the GPU tracer refuses v3 scenes. `material-gallery
  --pt` records the gallery's error against the tracer, the last piece of M3
  criterion 1.
