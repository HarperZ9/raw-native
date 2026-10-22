- **GPU timestamp queries** (`rhi::CommandList::timestamp`, `rhi::Device::timestampSeconds`,
  `raw_native_cli gpu-timestamps`). D3D12 query heaps and Vulkan query pools time GPU work
  without submission latency; `material-gallery` reports `dispatch_gpu_ms`. On an RTX 4090
  the sphere field's 1080p shading pass takes 2.40 ms, and two runs at 256 x 160 agree to
  0.2%, where the submit-to-fence timer disagreed by 74%.
