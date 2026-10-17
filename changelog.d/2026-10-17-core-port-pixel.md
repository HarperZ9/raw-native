- **Perspective-stable pixel art in the C++ core** (ADR 0019). The raymarched probe, the texel
  shading with posterised OKLab and outlines, the two atomic splat passes, the eye-ray fill and the
  cell crossfade run as one frame graph; the shader translator learned `atomicMin` as a statement.
  The C++ equals the JavaScript on every texel, pixel and code of five cases, and four of them are
  bit-identical on WARP. One WGSL source-edit control stayed inside the web bound and is recorded
  as a miss.
