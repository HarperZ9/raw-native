// The GPU side of world.mjs: the start-visibility pass (sv) and the merge helpers of the level pass
// (children, traced). The merges' loops and sd live in wgsl3d-passes.mjs (LEVEL, SRAYS); lookup,
// chebVis, levelZeroIndex and the cg gathers in wgsl3d-core.mjs.
import { PARAMS, CORE, ID } from "./wgsl3d-core.mjs";

export const INSIDE = /* wgsl */ `
fn insideF(f: u32) -> bool { let back = (f >> 16u) & 15u; let front = (f >> 20u) & 15u; return back >= 3u && back > front; }
`;
export const MERGE = INSIDE + /* wgsl */ `
// s x s children, s = c(l1) / c; w: their start visibility (1 without sv), which also weighs them.
fn children(l1: u32, q: u32, k: u32, c: u32) -> vec4f {
  let cc = u32(P.geo[l1].y); let sp = cc / c; let f = k / (c * c); let r = k - f * c * c; let j = r / c; let i = r - j * c;
  let tex = P.ofs[l1].z; let oo = P.ofs[l1].x;
  var out = vec3f(0.0); var ws = 0.0; var ov = vec3f(0.0); var wv = 0.0;
  for (var b = 0u; b < sp; b++) { for (var a = 0u; a < sp; a++) {
    let ti = sp * i + a; let tj = sp * j + b; let w = P.omega[oo + tj * cc + ti];
    let e = next[q * tex + f * cc * cc + tj * cc + ti];
    out += w * e.xyz; ws += w; ov += w * e.w * e.xyz; wv += w * e.w;
  } }
  if (P.wo.y == 0u) { return vec4f(out / ws, 1.0); }
  return select(vec4f(out / ws, 0.0), vec4f(ov / wv, wv / ws), wv > 0.0);
}
fn traced(o: vec3f, d: vec3f, t0: f32, t1: f32, found: ptr<function, bool>) -> vec3f {
  let h = trace(o, d, t0, t1, false);
  *found = h.slot >= 0;
  if (!*found) { return vec3f(0.0); }
  return surfaceRadiance(surfaceAt(o, d, h), d, P.sub0 == 1u);
}
`;

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
