- **Style passes in the C++ core, round 2** (ADR 0019). The post stack's driver now runs in the
  core (`raw/renderer/post_stack.hpp`): the pass registry, the encoding contract and the run order
  of the web stack, as a host frame graph on CPU references or a device frame graph on the shared
  WGSL. Four more passes have float64 C++ references and one WGSL source: CRT purity and degauss,
  pigment-space lighting, interference glazes and curvature hatching. On the exported web fixtures
  the C++ matches the JavaScript to 2.1e-14 with no 8-bit code different, and the GPU on WARP
  matches the C++ within the web bounds (at most 2 codes, mean at most 8.8e-4); the driver's 12
  plans equal the web plans. Nine of ten source-edit controls
  failed as expected. The pigment tables are generated from
  the JavaScript and checked in CI.
