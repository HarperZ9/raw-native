- **Spectral caustics through measured glass** (`docs/hw-rt.md`, `raw_native_cli caustic`). A prism
  of SCHOTT N-SF11 and a lens of N-BK7 split and focus illuminant A by the maker's Sellmeier data, one
  wavelength a path, with camera and light paths on the CPU and on hardware rays. A 4K frame of one
  sample and one photon a pixel takes 2.33 ms and 1.3 J on an RTX 4090. The hardware matches the CPU
  reference within its contract; the check of the two estimators against each other still fails per
  pixel while their image sums agree within 0.04%.
