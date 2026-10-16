- **The RHI on Vulkan** (RELEASE-PLAN N3, criteria 1 and 4). A Vulkan 1.3 backend with buffers,
  textures, samplers, compute and raster pipelines, render passes through dynamic rendering, and a
  swapchain on Win32, X11 (xcb) and Wayland (xdg-shell) windows. `raw_native_present_vk`, built with
  `RAW_NATIVE_VULKAN_RHI=ON`, runs the N2 presentation checks on it, from the same HLSL compiled to
  SPIR-V by glslang. On lavapipe in Ubuntu CI, with the validation layer on, 600 presented frames
  match the offscreen render and the D3D12 WARP render of N2 hash for hash, on X11 and on Wayland.
  A one-texel blit offset fails every frame. Resize, minimise (X11), fullscreen and a device loss
  injected at the RHI all recover. Vulkan has no call that removes a device, so recovery from a
  driver's own loss is not checked. Not in this change: HDR10 on Vulkan, the identity matrix and
  the N1 passes on Vulkan, and Steam Deck. Bounds were committed before the first run:
  `evidence/n3-vulkan-raster-bounds.json`.
