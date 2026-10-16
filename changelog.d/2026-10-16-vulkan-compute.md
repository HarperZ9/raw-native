- **The renderer on Vulkan** (RELEASE-PLAN N3, criteria 2 and 3). Every renderer pass now also
  builds as SPIR-V, from the same HLSL that `scripts/wgsl_to_hlsl.py` generates from the one WGSL
  source, compiled by glslang. Nothing is translated twice and no pass is edited.
  `raw_native_cli_vk` (`RAW_NATIVE_VULKAN_RHI=ON`) runs the renderer's checks on the Vulkan backend,
  which decorates every float add, multiply and dot product NoContraction so no driver fuses them,
  as `-Gis` does for D3D12. On lavapipe the quick identity matrix verifies 4 of 4, the software
  rasterizer is bit-exact, the M2 and M3 checks pass their WARP gates, and the style passes pass
  their ADR 0019 bounds. Missed and recorded: lighting shading slightly over its bound (3 of 4,096
  samples) and the shadow-map identity's depth bound on two scenes. Bounds:
  `evidence/n3-vulkan-compute-bounds.json`.
