- **Sphere field** (`raw_native_cli material-gallery --field`). A showcase and regression
  scene of 24 spheres in twelve looks (metals, plastics, clearcoat, sheen, anisotropy,
  thin-film iridescence, thin and absorbing glass, an emissive sphere) on a ground plane
  under the sky and a low sun. The float64 frame is pinned per compiler, the GPU shading
  matches it on the RTX 4090 (worst 5e-4) and WARP, and its error against path tracer v3
  is 0.23 (bound 0.6), most of it in the glass.
