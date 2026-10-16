- **Hysteresis quantisation and palette-locked lighting in the C++ core** (ADR 0019). Both passes
  have float64 C++ references and one WGSL source; hysteresis carries its decisions from frame to
  frame on the CPU and in two GPU state buffers. On the web fixtures every decision and 8-bit code
  equals the JavaScript (2,048,000 hysteresis decisions over 32 frames; five Chapel Yard cases,
  with the ramp palette built by the C++ and equal to the JavaScript one), and WARP matches the
  C++ with 0 decisions different. One built-in control could not fail on the flicker sequence and
  was replaced before any GPU run; one source-edit miss is recorded.
