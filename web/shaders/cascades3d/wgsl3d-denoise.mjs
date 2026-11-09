// The denoise passes of the 3D cascades on the GPU (denoise3d.mjs is the float64 reference).
// ATROUS (thread a probe): one a-trous pass of step P.level over the screen probes.
// ACCUM (thread a pixel): this frame's irradiance blended with the reprojected history, then shaded.
import { PARAMS, CORE } from "./wgsl3d-core.mjs";
import { ID, PROBE_PIXEL, SHADE } from "./wgsl3d-passes.mjs";

// exp(-t) for t >= 0 without the adapter's exp (see wgsl3d-core.mjs on loose transcendentals).
const EXPN = /* wgsl */ `
fn expN(t0: f32) -> f32 {
  let t = min(t0, 40.0); let k = u32(t); let f = t - f32(k);
  var r = 1.0 - f * (1.0 - f / 2.0 * (1.0 - f / 3.0 * (1.0 - f / 4.0 * (1.0 - f / 5.0 * (1.0 - f / 6.0 * (1.0 - f / 7.0 * (1.0 - f / 8.0))))))); 
  for (var i = 0u; i < k; i++) { r *= 0.36787944117144233; }
  return r;
}
fn lumY(e: vec3f) -> f32 { return dot(e, vec3f(0.2126, 0.7152, 0.0722)); }
`;

export const ATROUS = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> G: array<vec4f>;
@group(0) @binding(3) var<storage, read> Ein: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> Eout: array<vec4f>;
` + PROBE_PIXEL + EXPN + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let c = Ein[id];
  if (c.w <= 0.0) { Eout[id] = c; return; }
  let s = i32(P.level); let i = i32(id % P.pw); let j = i32(id / P.pw); let x = probeSurf(probePixel(id));
  let tol = 4.0 * f32(P.stride) * f32(s) * lenR(x.p - P.eye.xyz) * 2.0 * P.eye.w / f32(P.H) + 1e-3;
  let lc = lumY(c.xyz); var acc = vec3f(0.0); var ws = 0.0;
  for (var dy = -1; dy <= 1; dy++) { for (var dx = -1; dx <= 1; dx++) {
    let ii = i + dx * s; let jj = j + dy * s;
    if (ii < 0 || jj < 0 || ii >= i32(P.pw) || jj >= i32(P.ph)) { continue; }
    let qi = u32(jj) * P.pw + u32(ii); let q = Ein[qi];
    if (q.w <= 0.0) { continue; }
    let qs = probeSurf(probePixel(qi)); let nn = dot(x.n, qs.n);
    if (!(nn > 0.5 && abs(dot(qs.p - x.p, x.ng)) <= tol)) { continue; }
    var w = select(0.25, 0.5, dx == 0) * select(0.25, 0.5, dy == 0) * nn * nn * nn * nn;
    if (P.dnf.x > 0.0) { let lq = lumY(q.xyz); let r = abs(lq - lc) / max(max(lc, lq), 1e-6) / P.dnf.x; w *= expN(r * r); }
    if (w <= 0.0) { continue; }
    acc += w * q.xyz; ws += w;
  } }
  Eout[id] = vec4f(acc / ws, 1.0);
}`;

// Flag 1: history on; flag 8: write the irradiance instead of the radiance (the planted check).
export const ACCUM = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> G: array<vec4f>;
@group(0) @binding(3) var<storage, read> Gp: array<vec4f>;
@group(0) @binding(4) var<storage, read> I: array<vec4f>;
@group(0) @binding(5) var<storage, read> Hp: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> Hn: array<vec4f>;
@group(0) @binding(7) var<storage, read_write> outp: array<vec4f>;
` + SHADE + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let g1 = G[3u * id + 1u];
  if (g1.w < 0.0) { Hn[id] = vec4f(0.0); outp[id] = vec4f(0.0); return; }
  let px = id % P.W; let py = id / P.W; let g0 = G[3u * id];
  let x = Surf(g0.xyz, g1.xyz, G[3u * id + 2u].xyz, false, u32(g1.w));
  var e = I[id].xyz; var n = 1.0;
  let v = x.p - P.pEye.xyz; let z = dot(v, P.pF.xyz);
  if ((P.dn.z & 1u) != 0u && z > 0.0) {
    let fx = (dot(v, P.pS.xyz) / (z * P.pEye.w * P.pF.w) + 1.0) * 0.5 * f32(P.W) - 0.5;
    let fy = (1.0 - dot(v, P.pU.xyz) / (z * P.pEye.w)) * 0.5 * f32(P.H) - 0.5;
    let x0 = floor(fx); let y0 = floor(fy); let ax = fx - x0; let ay = fy - y0;
    let tol = 4.0 * lenR(x.p - P.eye.xyz) * 2.0 * P.eye.w / f32(P.H) + 1e-3;
    var he = vec3f(0.0); var hn = 0.0; var ws = 0.0;
    for (var c = 0u; c < 4u; c++) {
      let xi = i32(x0) + i32(c & 1u); let yi = i32(y0) + i32(c >> 1u);
      if (xi < 0 || yi < 0 || xi >= i32(P.W) || yi >= i32(P.H)) { continue; }
      let w = select(1.0 - ax, ax, (c & 1u) == 1u) * select(1.0 - ay, ay, (c >> 1u) == 1u);
      let o = u32(yi) * P.W + u32(xi); let q1 = Gp[3u * o + 1u];
      if (w <= 0.0 || q1.w < 0.0) { continue; }
      if (dot(x.n, q1.xyz) < 0.9 || abs(dot(Gp[3u * o].xyz - x.p, x.ng)) > tol) { continue; }
      let h = Hp[o]; he += w * h.xyz; hn += w * h.w; ws += w;
    }
    if (ws > 1e-3) { let hh = he / ws; n = min(hn / ws + 1.0, f32(P.dn.w)); e = hh + (e - hh) / n; }
  }
  Hn[id] = vec4f(e, n);
  if ((P.dn.z & 8u) != 0u) { outp[id] = vec4f(e, 1.0); return; }
  outp[id] = vec4f(shade(x, pixelDir(px, py), g0.w, e), 1.0);
}`;
