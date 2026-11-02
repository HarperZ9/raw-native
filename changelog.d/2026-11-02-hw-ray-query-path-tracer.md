- **Hardware ray tracing for the path tracer** (`docs/hw-rt.md`, `raw_native_cli hw-pt`). The path
  tracer runs on DXR inline ray queries with its BVH traversal as the only change (`hw_pt.hlsl` is
  generated from `pt_trace.hlsl`, and CI checks the two stay in step). At 3840 x 2160 and 64 spp on an
  RTX 4090 it renders raw_hall in 2.0 s against the compute tracer's 15.4 s. A tolerance contract
  against the CPU tracer holds on three scenes, and its three planted bugs fail it. `RayCaster`
  (`raw/rhi/hw_rt.hpp`) offers the same hardware as a library: build, refit and closest hits.
