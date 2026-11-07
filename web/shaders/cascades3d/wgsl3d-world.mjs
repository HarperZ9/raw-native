// The GPU side of world.mjs: the start-visibility pass (sv). The weighted merges live in
// wgsl3d-passes.mjs (children, SRAYS) and wgsl3d-core.mjs (lookup, chebVis, levelZeroIndex).
import { PARAMS, CORE } from "./wgsl3d-core.mjs";
import { ID } from "./wgsl3d-passes.mjs";

// Start visibility of level P.level (cascades3d.mjs startPass): thread = probe * texels + texel.
export const START = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read_write> sv: array<f32>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let l = P.level; let tex = P.ofs[l].z; let q = id / tex; let k = id % tex; let c = u32(P.geo[l].y); let T0 = P.geo[l].z;
  let p = probePos(l, q); var n = 0.0;
  if (P.wo.y == 2u) { sv[P.ofs[l].w + id] = select(1.0, 0.0, T0 > 0.0 && occluded(p, texelDir(k, c), T0)); return; } // sv 2: the entry's own ray
  for (var r = 0u; r < 4u; r++) { if (T0 <= 0.0 || !occluded(p, subTexelDir(k, c, r & 1u, r >> 1u), T0)) { n += 1.0; } }
  sv[P.ofs[l].w + id] = n * 0.25;
}`;
