- **Workgroup memory in the shader translator; sag and caricature in the C++ core** (ADR 0019).
  `scripts/wgsl_to_hlsl.py` now translates `var<workgroup>` arrays to `groupshared`,
  `workgroupBarrier()` and the group builtins, with selftests. Scan-causal EHT sag sums each
  line's beam current and the salience caricature finds its global maximum by trees in workgroup
  memory. Both match the JavaScript to the f32 bit on the CPU and are within 1 code on WARP. All
  four source-edit controls failed as predicted. `style-parity --only workgroup` runs the two.
