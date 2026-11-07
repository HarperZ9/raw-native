// The compute mains of the 3D cascades (one module each: PARAMS + CORE [+ GATHER] + main).
// Dispatches are 2D: invocation id = gid.y * P.gridX + gid.x, below P.total.
import { PARAMS, CORE, GATHER } from "./wgsl3d-core.mjs";

export const ID = /* wgsl */ `let id = gid.y * P.gridX + gid.x; if (id >= P.total) { return; } _ = scene[0];`;

// Primary hits: 3 vec4 a pixel: (p, sun scale), (n, slot or -1), (ng, back).
export const GBUF = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read_write> G: array<vec4f>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let d = pixelDir(id % P.W, id / P.W); let h = trace(P.eye.xyz, d, 0.0, 1e30, false);
  if (h.slot < 0) { G[3u * id] = vec4f(0.0); G[3u * id + 1u] = vec4f(0.0, 0.0, 0.0, -1.0); G[3u * id + 2u] = vec4f(d, 0.0); return; }
  let s = surfaceAt(P.eye.xyz, d, h);
  G[3u * id] = vec4f(s.p, sunScale(s)); G[3u * id + 1u] = vec4f(s.n, f32(s.slot)); G[3u * id + 2u] = vec4f(s.ng, 0.0);
}`;

// Probe flags of level P.level, one thread a (probe, ray): rays 0-5 are the axis rays (their hit
// counts, from behind in bits 16-19 and from the front in 20-23), rays 6-13 the segments to the next
// level's 8 neighbours (bit c when seen). The host clears the buffer first. insideF reads it.
export const FLAGS = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read_write> flags: array<atomic<u32>>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let l = P.level; let pr = id / 14u; let r = id % 14u; let p = probePos(l, pr); let h = P.geo[l].x; let at = P.ofs[l].y + pr;
  if (r < 6u) {
    var d = vec3f(0.0); d[r >> 1u] = select(1.0, -1.0, (r & 1u) == 1u);
    let t = trace(p, d, 0.0, h, false);
    if (t.slot >= 0) { atomicAdd(&flags[at], select(1u << 20u, 1u << 16u, surfaceAt(p, d, t).back)); }
    return;
  }
  if (l + 1u >= P.nLevels) { return; }
  let c = r - 6u; let q = neighbour(l + 1u, p, c); let e = q.pos - p; let len = lenR(e);
  if (len == 0.0 || !occluded(p, e / len, len)) { atomicOr(&flags[at], 1u << c); }
}`;

// Depth moments of level 0's probes (static geometry: once).
export const DEPTH = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read_write> depth: array<vec2f>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let q = id / P.depthTexels; let k = id % P.depthTexels; let p = probePos(0u, q); let dmax = P.sunE.w;
  var m = vec2f(0.0);
  for (var r = 0u; r < 4u; r++) {
    let h = trace(p, subTexelDir(k, P.cd, r & 1u, r >> 1u), 0.0, dmax, false);
    let t = select(dmax, h.t, h.slot >= 0);
    m += vec2f(t, t * t) * 0.25;
  }
  depth[id] = m;
}`;

const INSIDE = /* wgsl */ `
fn insideF(f: u32) -> bool { let back = (f >> 16u) & 15u; let front = (f >> 20u) & 15u; return back >= 3u && back > front; }
`;
const MERGE = INSIDE + /* wgsl */ `
fn children(l1: u32, q: u32, k: u32, c: u32) -> vec3f {
  let cc = 2u * c; let f = k / (c * c); let r = k - f * c * c; let j = r / c; let i = r - j * c;
  let tex = P.ofs[l1].z; let oo = P.ofs[l1].x;
  var out = vec3f(0.0); var ws = 0.0;
  for (var b = 0u; b < 2u; b++) { for (var a = 0u; a < 2u; a++) {
    let ti = 2u * i + a; let tj = 2u * j + b; let w = P.omega[oo + tj * cc + ti];
    out += w * next[q * tex + f * cc * cc + tj * cc + ti].xyz; ws += w;
  } }
  return out / ws;
}
fn traced(o: vec3f, d: vec3f, t0: f32, t1: f32, found: ptr<function, bool>) -> vec3f {
  let h = trace(o, d, t0, t1, false);
  *found = h.slot >= 0;
  if (!*found) { return vec3f(0.0); }
  return surfaceRadiance(surfaceAt(o, d, h), d, P.sub0 == 1u);
}
`;

// One entry of level P.level (cascades3d.mjs entry): thread = probe * texels + texel.
export const LEVEL = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> cache: array<vec4f>;
@group(0) @binding(3) var<storage, read> depth: array<vec2f>;
@group(0) @binding(4) var<storage, read> flags: array<u32>;
@group(0) @binding(5) var<storage, read> next: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> cur: array<vec4f>;
` + GATHER + MERGE + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  _ = next[0]; _ = flags[0];
  let l = P.level; let tex = P.ofs[l].z; let idx = id / tex; let k = id % tex; let c = u32(P.geo[l].y);
  let p = probePos(l, idx); let d = texelDir(k, c); let top = l + 1u == P.nLevels; let T0 = P.geo[l].z; let T1x = P.geo[l].w;
  var found = false;
  if (P.fix == 2u && !top) {
    let T1 = P.geo[l + 1u].z; let s = p + d * T0; var out = vec3f(0.0);
    for (var cn = 0u; cn < 8u; cn++) {
      let q = neighbour(l + 1u, p, cn);
      if (q.w <= 0.0) { continue; }
      let e = q.pos + d * T1 - s; let len = lenR(e);
      var v = traced(s, e / len, 0.0, len, &found);
      if (!found) { v = children(l + 1u, q.idx, k, c); }
      out += q.w * v;
    }
    cur[id] = vec4f(out, 0.0); return;
  }
  var v = traced(p, d, T0, T1x, &found);
  if (found) { cur[id] = vec4f(v, 0.0); return; }
  if (top) { cur[id] = vec4f(P.sky.xyz, 0.0); return; }
  let bits = flags[P.ofs[l].y + idx];
  var out = vec3f(0.0); var all = vec3f(0.0); var ws = 0.0; var wa = 0.0;
  for (var cn = 0u; cn < 8u; cn++) {
    let q = neighbour(l + 1u, p, cn);
    if (q.w <= 0.0) { continue; }
    let m = children(l + 1u, q.idx, k, c);
    all += q.w * m; wa += q.w;
    if (P.fix == 1u && ((bits & (1u << cn)) == 0u || insideF(flags[P.ofs[l + 1u].y + q.idx]))) { continue; }
    out += q.w * m; ws += q.w;
  }
  cur[id] = vec4f(select(all / max(wa, 1e-30), out / ws, ws > 0.0), 0.0);
}`;

// Level 0's merged radiance as the L1 irradiance cache (cascades3d.mjs cacheOf): thread = probe.
export const CACHE = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> buf0: array<vec4f>;
@group(0) @binding(3) var<storage, read> flags: array<u32>;
@group(0) @binding(4) var<storage, read_write> cacheOut: array<vec4f>;
` + INSIDE + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let c = u32(P.geo[0].y); let tex = P.ofs[0].z; let cc = c * c;
  let k0 = PI * 0.28209479177387814 * 0.28209479177387814; let k1 = 2.0 * PI / 3.0 * 0.4886025119029199 * 0.4886025119029199;
  var a = vec3f(0.0); var bx = vec3f(0.0); var by = vec3f(0.0); var bz = vec3f(0.0);
  for (var k = 0u; k < tex; k++) {
    let d = texelDir(k, c); let v = P.omega[P.ofs[0].x + k % cc] * buf0[id * tex + k].xyz;
    a += k0 * v; bx += k1 * v * d.x; by += k1 * v * d.y; bz += k1 * v * d.z;
  }
  cacheOut[4u * id] = vec4f(a, select(0.0, 1.0, insideF(flags[P.ofs[0].y + id])));
  cacheOut[4u * id + 1u] = vec4f(bx, 0.0); cacheOut[4u * id + 2u] = vec4f(by, 0.0); cacheOut[4u * id + 3u] = vec4f(bz, 0.0);
}`;

const LOOKUP = /* wgsl */ `
fn lookup(q: u32, d: vec3f) -> vec3f { // level 1's merged radiance at probe q, bilinear in the face
  let c = u32(P.geo[1].y); let tex = P.ofs[1].z; let F = faceOf(d); let cf = f32(c);
  if (c == 1u) { return next[q * tex + F.f].xyz; }
  let u = clamp((F.s + 1.0) / 2.0 * cf - 0.5, 0.0, cf - 1.0); let v = clamp((F.t + 1.0) / 2.0 * cf - 0.5, 0.0, cf - 1.0);
  let i0 = min(c - 2u, u32(floor(u))); let j0 = min(c - 2u, u32(floor(v))); let fu = u - f32(i0); let fv = v - f32(j0);
  let b = q * tex + F.f * c * c + j0 * c + i0;
  return (1.0 - fu) * (1.0 - fv) * next[b].xyz + fu * (1.0 - fv) * next[b + 1u].xyz + (1.0 - fu) * fv * next[b + c].xyz + fu * fv * next[b + c + 1u].xyz;
}
`;

// Screen probe (i, j) of stride P.stride sits on pixel (k i + off, k j + off). Three passes:
// SVIS (thread a probe): which level-1 neighbours the probe sees, outside geometry (fix "probe"), or
// all 8 (fix "none"); bit 8 marks a probe on a surface. SRAYS (thread a probe and ray): one cosine
// ray of cascades3d.mjs screenProbe. SSUM (thread a probe): E = pi / m^2 x the sum, valid in w.
export const PROBE_PIXEL = /* wgsl */ `
fn probePixel(id: u32) -> u32 { let k = P.stride; return min(P.H - 1u, k * (id / P.pw) + (P.dn.y >> 16u)) * P.W + min(P.W - 1u, k * (id % P.pw) + (P.dn.y & 0xffffu)); }
fn probeSurf(g: u32) -> Surf { let g1 = G[3u * g + 1u]; return Surf(G[3u * g].xyz, g1.xyz, G[3u * g + 2u].xyz, false, u32(max(g1.w, 0.0))); }
`;
export const SVIS = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> flags: array<u32>;
@group(0) @binding(3) var<storage, read> G: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> bits: array<u32>;
` + INSIDE + PROBE_PIXEL + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  _ = flags[0];
  let g = probePixel(id);
  if (G[3u * g + 1u].w < 0.0) { bits[id] = 0u; return; }
  let x = probeSurf(g);
  var b = 256u;
  if (P.nLevels > 1u && P.fix == 1u) {
    let up = offsetP(x, x.ng);
    for (var c = 0u; c < 8u; c++) {
      let q = neighbour(1u, x.p, c); let e = q.pos - up; let len = lenR(e);
      if ((len == 0.0 || !occluded(up, e / len, len)) && !insideF(flags[P.ofs[1].y + q.idx])) { b |= 1u << c; }
    }
  } else { b |= 255u; }
  bits[id] = b;
}`;
export const SRAYS = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> cache: array<vec4f>;
@group(0) @binding(3) var<storage, read> depth: array<vec2f>;
@group(0) @binding(4) var<storage, read> next: array<vec4f>;
@group(0) @binding(5) var<storage, read> G: array<vec4f>;
@group(0) @binding(6) var<storage, read> bits: array<u32>;
@group(0) @binding(7) var<storage, read_write> R: array<vec4f>;
` + GATHER + MERGE + LOOKUP + PROBE_PIXEL + /* wgsl */ `
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  _ = next[0];
  let m = P.m; let M = m * m; let pr = id / M; let kk = id % M; let i = kk % m; let j = kk / m;
  let b = bits[pr];
  if ((b & 256u) == 0u) { R[id] = vec4f(0.0); return; }
  let x = probeSurf(probePixel(pr));
  let one = P.nLevels > 1u; let T1 = P.geo[min(1u, P.nLevels - 1u)].z; let T1x = P.geo[0].w;
  let jt = jitter(pr ^ P.dn.x, j * m + i); let d = cosineDir(x.n, i, j, m, jt.x, jt.y); let o = offsetP(x, d);
  var v = vec3f(0.0); var found = false;
  if (one && P.fix == 2u) {
    for (var c = 0u; c < 8u; c++) {
      let q = neighbour(1u, x.p, c);
      if (q.w <= 0.0) { continue; }
      let e = q.pos + d * T1 - o; let len = lenR(e);
      var r = traced(o, e / len, 0.0, len, &found);
      if (!found) { r = lookup(q.idx, d); }
      v += q.w * r;
    }
  } else {
    v = traced(o, d, 0.0, select(1e30, T1x, one), &found);
    if (!found) {
      if (!one) { v = P.sky.xyz; }
      else {
        var out = vec3f(0.0); var all = vec3f(0.0); var ws = 0.0; var wa = 0.0;
        for (var c = 0u; c < 8u; c++) {
          let q = neighbour(1u, x.p, c);
          if (q.w <= 0.0) { continue; }
          let r = lookup(q.idx, d); all += q.w * r; wa += q.w;
          if ((b & (1u << c)) == 0u) { continue; }
          out += q.w * r; ws += q.w;
        }
        v = select(all / max(wa, 1e-30), out / ws, ws > 0.0);
      }
    }
  }
  R[id] = vec4f(v, 0.0);
}`;
// Flag 2 (the planted check of denoise): every probe holds dnf.z below x = dnf.y and dnf.w above.
export const SSUM = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> bits: array<u32>;
@group(0) @binding(3) var<storage, read> R: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> E: array<vec4f>;
@group(0) @binding(5) var<storage, read> G: array<vec4f>;
` + PROBE_PIXEL + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  if ((bits[id] & 256u) == 0u) { E[id] = vec4f(0.0); return; }
  if ((P.dn.z & 2u) != 0u) { E[id] = vec4f(vec3f(select(P.dnf.w, P.dnf.z, G[3u * probePixel(id)].x < P.dnf.y)), 1.0); return; }
  let M = P.m * P.m; var sum = vec3f(0.0);
  for (var k = 0u; k < M; k++) { sum += R[id * M + k].xyz; }
  E[id] = vec4f(sum * (PI / f32(M)), 1.0);
}`;

export const SHADE = /* wgsl */ `
fn shade(x: Surf, d: vec3f, sun: f32, Egi: vec3f) -> vec3f {
  let em = emission(x.slot);
  if (dot(x.n, d) >= 0.0) { return em; }
  return em + albedo(x.slot) / PI * (P.sunE.xyz * sun + Egi);
}
`;

// Every pixel (run3d.mjs pixelE): screen probes upsampled, else the cache gather; out (rgb, hit), or
// with flag 4 the irradiance (for temporal accumulation, wgsl3d-denoise.mjs ACCUM).
export const FINAL = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> cache: array<vec4f>;
@group(0) @binding(3) var<storage, read> depth: array<vec2f>;
@group(0) @binding(4) var<storage, read> G: array<vec4f>;
@group(0) @binding(5) var<storage, read> E: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> outp: array<vec4f>;
` + GATHER + SHADE + /* wgsl */ `
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  _ = E[0];
  let g1 = G[3u * id + 1u];
  if (g1.w < 0.0) { outp[id] = vec4f(0.0); return; }
  let px = id % P.W; let py = id / P.W; let g0 = G[3u * id];
  let x = Surf(g0.xyz, g1.xyz, G[3u * id + 2u].xyz, false, u32(g1.w));
  var Egi = vec3f(0.0);
  var ws = 0.0;
  if (P.stride > 0u) {
    let k = f32(P.stride); let ox = P.dn.y & 0xffffu; let oy = P.dn.y >> 16u;
    let u = max(0.0, (f32(px) - f32(ox)) / k); let v = max(0.0, (f32(py) - f32(oy)) / k);
    let i0 = min(P.pw - 1u, u32(floor(u))); let j0 = min(P.ph - 1u, u32(floor(v))); let fu = u - f32(i0); let fv = v - f32(j0);
    let tol = 4.0 * k * lenR(x.p - P.eye.xyz) * 2.0 * P.eye.w / f32(P.H) + 1e-3;
    for (var c = 0u; c < 4u; c++) {
      let i = i0 + (c & 1u); let j = j0 + (c >> 1u);
      if (i >= P.pw || j >= P.ph) { continue; }
      let w = select(1.0 - fu, fu, (c & 1u) == 1u) * select(1.0 - fv, fv, (c >> 1u) == 1u);
      let e = E[j * P.pw + i];
      if (w <= 0.0 || e.w <= 0.0) { continue; }
      let qp = min(P.H - 1u, P.stride * j + oy) * P.W + min(P.W - 1u, P.stride * i + ox);
      let qn = G[3u * qp + 1u].xyz; let nn = dot(x.n, qn);
      if (nn > 0.5 && abs(dot(G[3u * qp].xyz - x.p, x.ng)) <= tol) { let ww = w * nn * nn * nn * nn; Egi += ww * e.xyz; ws += ww; }
    }
  }
  if (ws > 0.0) { Egi = Egi / ws; } else { Egi = gatherCache(x); }
  if ((P.dn.z & 4u) != 0u) { outp[id] = vec4f(Egi, 1.0); return; }
  outp[id] = vec4f(shade(x, pixelDir(px, py), g0.w, Egi), 1.0);
}`;

// The no-GI baseline (run3d.mjs noGiBaseline): sun + pi sky x the cosine share of P.m^2 rays that escape.
export const BASELINE = PARAMS + CORE + /* wgsl */ `
@group(0) @binding(2) var<storage, read> G: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> outp: array<vec4f>;
` + SHADE + /* wgsl */ `
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${ID}
  let g1 = G[3u * id + 1u];
  if (g1.w < 0.0) { outp[id] = vec4f(0.0); return; }
  let g0 = G[3u * id]; let x = Surf(g0.xyz, g1.xyz, G[3u * id + 2u].xyz, false, u32(g1.w)); let m = P.m;
  var open = 0u;
  for (var j = 0u; j < m; j++) { for (var i = 0u; i < m; i++) {
    let jt = jitter(id, j * m + i); let d = cosineDir(x.n, i, j, m, jt.x, jt.y);
    if (!occluded(offsetP(x, d), d, 1e30)) { open++; }
  } }
  outp[id] = vec4f(shade(x, pixelDir(id % P.W, id / P.W), g0.w, PI * P.sky.xyz * f32(open) / f32(m * m)), 1.0);
}`;
