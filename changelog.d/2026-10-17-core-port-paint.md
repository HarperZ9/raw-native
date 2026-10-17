- **The painterly shader in the C++ core** (ADR 0019). Prep to a display colour and its
  Kubelka-Munk pigment latent, the smoothed structure tensor, the anisotropic Kuwahara filter on
  pigment latents, flow-following brush relief and stroke identity, XDoG lines and lit impasto, and
  compose with the watercolour glaze run as one frame graph of `style.wgsl` passes; the advected
  canvas for a moving camera (decide, advect, distortion) runs on the GPU each frame. The C++ equals
  the JavaScript on every image value and code of fifteen cases (stills, camera sequences and the
  960 x 600 post frames). The first WARP run failed two watercolour post frames: WARP's `sinh` and
  `cosh` were imprecise enough to flip a nearly cancelled glaze channel, and the WGSL now builds
  both from one `exp`.
