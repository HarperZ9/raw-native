// WGSL for the 3D radiance cascades: bvh.mjs, math3d.mjs and cascades3d.mjs in f32, step for step.
// A module is PARAMS + CORE + one main from wgsl3d-passes.mjs; each main declares its bindings after
// binding 0 (params) and 1 (scene: BVH nodes, then 8 vec4 a triangle from triOff: p0, p1, p2, n0, n1,
// n2, albedo, emission). Shaders compute sin, cos and sqrt themselves: WGSL lets an adapter compute
// them loosely (SwiftShader did, in the flatland path), which moves rays against the f64 reference.
export const PARAMS_WORDS = 80 + 4 * 8 * 3; // header and 16 vec4, then dims, geo and ofs (8 vec4 each)
export const FRAME_WORDS = [32, 72]; // the camera, denoise and previous-camera words, rewritten every frame

export const PARAMS = /* wgsl */ `
struct Params {
  W: u32, H: u32, nLevels: u32, level: u32,
  fix: u32, stride: u32, m: u32, cd: u32,
  total: u32, gridX: u32, triOff: u32, depthTexels: u32,
  pw: u32, ph: u32, sub0: u32, sub1: u32,
  lo: vec4f, sunDir: vec4f, sunE: vec4f, sky: vec4f,
  eye: vec4f, camF: vec4f, camS: vec4f, camU: vec4f,
  dn: vec4u,    // ray seed, probe offsets ox | oy << 16, flags (1 history on, 2 planted probes, 4 irradiance out), tmax
  dnf: vec4f,   // luminance edge stop sl, planted edge x, planted value below it, above it
  pEye: vec4f, pF: vec4f, pS: vec4f, pU: vec4f, // previous frame's camera (pEye.w tan half fovy, pF.w aspect)
  clo: vec4f,   // the grid origin of the cache this pass gathers from (lo, unless the grid moves: world.mjs jit)
  wo: vec4u,    // mw (0 seg, 1 vis), sv (1 or 2: merges weigh texels by start visibility, in level buffers' w), fz | sd << 1 (blocked entries trace from 0, written w 1) | pi << 2 | fd << 3 | sk << 5, ft | cg << 1
  dims: array<vec4u, 8>,
  geo: array<vec4f, 8>,
  ofs: array<vec4u, 8>,
  omega: array<f32>,
}
@group(0) @binding(0) var<storage, read> P: Params;
@group(0) @binding(1) var<storage, read> scene: array<vec4f>;
`;

export const ID = /* wgsl */ `let id = gid.y * P.gridX + gid.x; if (id >= P.total) { return; } _ = scene[0];`;

export const CORE = /* wgsl */ `
const PI = 3.14159265358979;
fn sqrtR(x: f32) -> f32 { if (x <= 0.0) { return 0.0; } let r = sqrt(x); return 0.5 * (r + x / r); }
fn nrm(v: vec3f) -> vec3f { let m = sqrtR(dot(v, v)); if (m > 0.0) { return v / m; } return vec3f(0.0); }
fn lenR(v: vec3f) -> f32 { return sqrtR(dot(v, v)); }
// Taylor series, enough terms for |x| <= 3 pi / 4.
fn sinP(x: f32) -> f32 { let z = x * x; return x * (1.0 - z / 6.0 * (1.0 - z / 20.0 * (1.0 - z / 42.0 * (1.0 - z / 72.0 * (1.0 - z / 110.0 * (1.0 - z / 156.0 * (1.0 - z / 210.0))))))); }
fn cosP(x: f32) -> f32 { let z = x * x; return 1.0 - z / 2.0 * (1.0 - z / 12.0 * (1.0 - z / 30.0 * (1.0 - z / 56.0 * (1.0 - z / 90.0 * (1.0 - z / 132.0 * (1.0 - z / 182.0 * (1.0 - z / 240.0))))))); }
fn hash32(x0: u32) -> u32 {
  let x = x0 * 747796405u + 2891336453u; let w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  return (w >> 22u) ^ w;
}

struct Hit { t: f32, u: f32, v: f32, slot: i32 }
fn invDir(d: vec3f) -> vec3f {
  var r = vec3f(0.0);
  for (var k = 0; k < 3; k++) { let x = d[k]; r[k] = 1.0 / select(x, select(1e-20, -1e-20, x < 0.0), abs(x) < 1e-20); }
  return r;
}
fn slab(i: u32, o: vec3f, inv: vec3f, tMin: f32, tMax: f32) -> f32 {
  let lo = scene[2u * i].xyz; let hi = scene[2u * i + 1u].xyz;
  let a = (lo - o) * inv; let b = (hi - o) * inv;
  let t0 = max(max(max(min(a.x, b.x), min(a.y, b.y)), min(a.z, b.z)), tMin);
  let t1 = min(min(min(max(a.x, b.x), max(a.y, b.y)), max(a.z, b.z)), tMax);
  return select(1e30, t0, t0 <= t1);
}
fn triHit(s: u32, o: vec3f, d: vec3f, tMin: f32, tMax: f32) -> vec3f { // (t, u, v); t < 0 on a miss
  let b = P.triOff + 8u * s; let p0 = scene[b].xyz;
  let e1 = scene[b + 1u].xyz - p0; let e2 = scene[b + 2u].xyz - p0;
  let p = cross(d, e2); let det = dot(e1, p);
  if (det == 0.0) { return vec3f(-1.0); }
  let id = 1.0 / det; let sv = o - p0; let u = dot(sv, p) * id;
  if (u < 0.0 || u > 1.0) { return vec3f(-1.0); }
  let q = cross(sv, e1); let v = dot(d, q) * id;
  if (v < 0.0 || u + v > 1.0) { return vec3f(-1.0); }
  let t = dot(e2, q) * id;
  if (t > tMin && t < tMax) { return vec3f(t, u, v); }
  return vec3f(-1.0);
}
fn trace(o: vec3f, d: vec3f, tMin: f32, tMax: f32, any: bool) -> Hit {
  var st: array<u32, 48>; var sp = 1u; st[0] = 0u;
  let inv = invDir(d);
  var best = Hit(tMax, 0.0, 0.0, -1);
  while (sp > 0u) {
    sp--; let i = st[sp];
    if (slab(i, o, inv, tMin, best.t) >= 1e30) { continue; }
    let A = bitcast<u32>(scene[2u * i].w); let cnt = bitcast<u32>(scene[2u * i + 1u].w);
    if (cnt > 0u) {
      for (var s = A; s < A + cnt; s++) {
        let h = triHit(s, o, d, tMin, best.t);
        if (h.x >= 0.0) { best = Hit(h.x, h.y, h.z, i32(s)); if (any) { return best; } }
      }
      continue;
    }
    let l = i + 1u; let r = A;
    let tl = slab(l, o, inv, tMin, best.t); let tr = slab(r, o, inv, tMin, best.t);
    if (tl <= tr) { if (tr < 1e30) { st[sp] = r; sp++; } if (tl < 1e30) { st[sp] = l; sp++; } }
    else { if (tl < 1e30) { st[sp] = l; sp++; } st[sp] = r; sp++; }
  }
  return best;
}
fn occluded(o: vec3f, d: vec3f, tMax: f32) -> bool { return trace(o, d, 0.0, tMax, true).slot >= 0; }

struct Surf { p: vec3f, n: vec3f, ng: vec3f, back: bool, slot: u32 }
fn surfaceAt(o: vec3f, d: vec3f, h: Hit) -> Surf {
  let s = u32(h.slot); let b = P.triOff + 8u * s; let a = scene[b].xyz;
  var ng = nrm(cross(scene[b + 1u].xyz - a, scene[b + 2u].xyz - a));
  let back = dot(ng, d) > 0.0;
  if (back) { ng = -ng; }
  let w = 1.0 - h.u - h.v;
  var n = nrm(scene[b + 3u].xyz * w + scene[b + 4u].xyz * h.u + scene[b + 5u].xyz * h.v);
  if (dot(n, ng) < 0.0) { n = -n; }
  return Surf(o + d * h.t, n, ng, back, s);
}
fn albedo(s: u32) -> vec3f { return scene[P.triOff + 8u * s + 6u].xyz; }
fn emission(s: u32) -> vec3f { return scene[P.triOff + 8u * s + 7u].xyz; }
fn offsetP(x: Surf, dir: vec3f) -> vec3f {
  let e = 2e-4 * (1.0 + max(abs(x.p.x), max(abs(x.p.y), abs(x.p.z))));
  return x.p + x.ng * select(-e, e, dot(x.ng, dir) > 0.0);
}
fn sunScale(x: Surf) -> f32 {
  if (P.sunDir.w <= 0.0) { return 0.0; }
  let l = P.sunDir.xyz; let c = dot(x.n, l);
  if (c > 0.0 && !occluded(offsetP(x, l), l, 1e30)) { return c; }
  return 0.0;
}

// Cube texels (math3d.mjs).
fn texelDir(k: u32, c: u32) -> vec3f {
  let cc = c * c; let f = k / cc; let r = k - f * cc; let j = r / c; let i = r - j * c;
  let m = f >> 1u; var d = vec3f(0.0);
  d[m] = select(1.0, -1.0, (f & 1u) == 1u);
  d[(m + 1u) % 3u] = (f32(i) + 0.5) / f32(c) * 2.0 - 1.0; d[(m + 2u) % 3u] = (f32(j) + 0.5) / f32(c) * 2.0 - 1.0;
  return nrm(d);
}
fn subTexelDir(k: u32, c: u32, a: u32, b: u32) -> vec3f {
  let cc = c * c; let f = k / cc; let r = k - f * cc; let j = r / c; let i = r - j * c;
  let m = f >> 1u; var d = vec3f(0.0);
  d[m] = select(1.0, -1.0, (f & 1u) == 1u);
  d[(m + 1u) % 3u] = (f32(i) + 0.25 + 0.5 * f32(a)) / f32(c) * 2.0 - 1.0; d[(m + 2u) % 3u] = (f32(j) + 0.25 + 0.5 * f32(b)) / f32(c) * 2.0 - 1.0;
  return nrm(d);
}
struct Face { f: u32, s: f32, t: f32 }
fn faceOf(d: vec3f) -> Face {
  var m = 0u;
  if (abs(d.y) > abs(d[m])) { m = 1u; }
  if (abs(d.z) > abs(d[m])) { m = 2u; }
  let a = abs(d[m]);
  return Face(2u * m + select(0u, 1u, d[m] < 0.0), d[(m + 1u) % 3u] / a, d[(m + 2u) % 3u] / a);
}
fn texelOf(d: vec3f, c: u32) -> u32 {
  let F = faceOf(d); let cf = f32(c);
  let i = u32(clamp(floor((F.s + 1.0) / 2.0 * cf), 0.0, cf - 1.0)); let j = u32(clamp(floor((F.t + 1.0) / 2.0 * cf), 0.0, cf - 1.0));
  return F.f * c * c + j * c + i;
}
fn probePos(l: u32, idx: u32) -> vec3f {
  let n = P.dims[l]; let x = idx % n.x; let y = (idx / n.x) % n.y; let z = idx / (n.x * n.y);
  return P.lo.xyz + vec3f(f32(x), f32(y), f32(z)) * P.geo[l].x;
}
struct Nb { idx: u32, w: f32, pos: vec3f }
fn neighbourLo(l: u32, p: vec3f, c: u32, lo: vec3f) -> Nb {
  let n = P.dims[l]; let h = P.geo[l].x; let nf = vec3f(f32(n.x), f32(n.y), f32(n.z));
  let g = clamp((p - lo) / h, vec3f(0.0), nf - 1.0);
  let b = min(floor(g), nf - 2.0); let fr = g - b;
  let o = vec3f(f32(c & 1u), f32((c >> 1u) & 1u), f32((c >> 2u) & 1u));
  let q = b + o; let w3 = select(1.0 - fr, fr, o > vec3f(0.5));
  return Nb(u32((q.z * nf.y + q.y) * nf.x + q.x), w3.x * w3.y * w3.z, lo + q * h);
}
fn neighbour(l: u32, p: vec3f, c: u32) -> Nb { return neighbourLo(l, p, c, P.lo.xyz); }
fn cosineDir(n: vec3f, i: u32, j: u32, m: u32, h1: f32, h2: f32) -> vec3f {
  let a = 2.0 * (f32(i) + h1) / f32(m) - 1.0; let b = 2.0 * (f32(j) + h2) / f32(m) - 1.0;
  var r = 0.0; var phi = 0.0;
  if (a != 0.0 || b != 0.0) {
    if (abs(a) > abs(b)) { r = a; phi = PI / 4.0 * (b / a); } else { r = b; phi = PI / 2.0 - PI / 4.0 * (a / b); }
  }
  let x = r * cosP(phi); let y = r * sinP(phi); let z = sqrtR(max(0.0, 1.0 - x * x - y * y));
  let sg = select(-1.0, 1.0, n.z >= 0.0); let aa = -1.0 / (sg + n.z); let bb = n.x * n.y * aa;
  let t = vec3f(1.0 + sg * n.x * n.x * aa, sg * bb, -sg * n.x); let bt = vec3f(bb, sg + n.y * n.y * aa, -n.y);
  return nrm(t * x + bt * y + n * z);
}
fn pixelDir(x: u32, y: u32) -> vec3f {
  let nx = (f32(x) + 0.5) / f32(P.W) * 2.0 - 1.0; let ny = 1.0 - (f32(y) + 0.5) / f32(P.H) * 2.0;
  return nrm(P.camF.xyz + P.camS.xyz * (nx * P.eye.w * P.camF.w) + P.camU.xyz * (ny * P.eye.w));
}
fn jitter(p: u32, k: u32) -> vec2f { let h = hash32(p * 0x9e3779b9u + k); let g = hash32(h); return vec2f(f32(h) / 4294967296.0, f32(g) / 4294967296.0); }
`;

// The cache gather (cascades3d.mjs gatherCache), for passes that bind cache and depth. The cache's
// grid starts at P.clo; depth holds that grid's moments.
export const GATHER = /* wgsl */ `
fn chebVis(q0: u32, qp: vec3f, at: vec3f) -> f32 { // world.mjs chebVis
  let v = at - qp; let dist = lenR(v); let h0 = P.lo.w;
  if (dist <= 0.0) { return 1.0; }
  let t = depth[q0 * P.depthTexels + texelOf(v / dist, P.cd)]; let mu = t.x; // mean, mean square, nearest, farthest
  if (dist <= mu || mu >= 0.999 * P.sunE.w) { return 1.0; }
  let va = max(t.y - mu * mu, 1e-4 * h0 * h0);
  var vis = va / (va + (dist - mu) * (dist - mu)); vis = vis * vis * vis;
  return select(vis, 0.0, vis < 0.05);
}
fn cacheE(q: u32, n: vec3f) -> vec3f {
  return max(vec3f(0.0), cache[4u * q].xyz + cache[4u * q + 1u].xyz * n.x + cache[4u * q + 2u].xyz * n.y + cache[4u * q + 3u].xyz * n.z);
}
// cg (cascades3d.mjs gatherSeen): lit-side probes outside geometry that a segment from the surface
// reaches; cg 5 and 6: the eligible probes in decreasing weight (the first on ties, as the CPU), up to
// 1 or 8 of them, stopping at the first a segment reaches.
fn seenOk(q: Nb, x: Surf) -> bool { return q.w > 0.0 && cache[4u * q.idx].w <= 0.0 && dot(q.pos - x.p, x.ng) > 0.0; }
fn gatherFirst(x: Surf, tries: u32) -> vec3f {
  let up = offsetP(x, x.ng); var used = 0u;
  for (var it = 0u; it < tries; it++) {
    var top = 8u; var tw = 0.0;
    for (var c = 0u; c < 8u; c++) { let q = neighbourLo(0u, x.p, c, P.clo.xyz); if ((used & (1u << c)) == 0u && seenOk(q, x) && (top == 8u || q.w > tw)) { top = c; tw = q.w; } }
    if (top == 8u) { break; }
    used |= 1u << top; let q = neighbourLo(0u, x.p, top, P.clo.xyz); let e = q.pos - up; let len = lenR(e);
    if (len <= 0.0 || !occluded(up, e / len, len)) { return cacheE(q.idx, x.n); }
  }
  return vec3f(0.0);
}
fn gatherSeen(x: Surf) -> vec3f {
  let cg = P.wo.w >> 1u; if (cg >= 5u) { return gatherFirst(x, select(8u, 1u, cg == 5u)); }
  let up = offsetP(x, x.ng); var E = vec3f(0.0); var ws = 0.0;
  for (var c = 0u; c < 8u; c++) {
    let q = neighbourLo(0u, x.p, c, P.clo.xyz);
    if (!seenOk(q, x)) { continue; }
    let e = q.pos - up; let len = lenR(e);
    if (len > 0.0 && occluded(up, e / len, len)) { continue; }
    E += q.w * cacheE(q.idx, x.n); ws += q.w;
  }
  if (ws > 0.0) { return E / ws; }
  return vec3f(0.0);
}
// cg 2, 3 (world.mjs gatherDepthSeen): no rays; the farthest (2) or nearest (3) depth ray must reach the point.
fn gatherDepthSeen(x: Surf) -> vec4f { // w: whether a probe was kept
  let at = x.p + x.ng * (0.25 * P.lo.w); let near = (P.wo.w >> 1u) == 3u; var E = vec3f(0.0); var ws = 0.0;
  for (var c = 0u; c < 8u; c++) {
    let q = neighbourLo(0u, x.p, c, P.clo.xyz);
    if (q.w <= 0.0 || cache[4u * q.idx].w > 0.0 || dot(q.pos - x.p, x.ng) <= 0.0) { continue; }
    let v = at - q.pos; let dist = lenR(v);
    if (dist > 0.0) { let t = depth[q.idx * P.depthTexels + texelOf(v / dist, P.cd)]; if (select(t.w, t.z, near) < dist) { continue; } }
    E += q.w * cacheE(q.idx, x.n); ws += q.w;
  }
  if (ws > 0.0) { return vec4f(E / ws, 1.0); }
  return vec4f(0.0);
}
fn gatherCache(x: Surf) -> vec3f {
  var E = vec3f(0.0); var El = vec3f(0.0); var Ef = vec3f(0.0); var Ea = vec3f(0.0);
  var ws = 0.0; var wl = 0.0; var wf = 0.0; var wa = 0.0;
  let h0 = P.lo.w; let at = x.p + x.ng * (0.25 * h0);
  for (var c = 0u; c < 8u; c++) {
    let q = neighbourLo(0u, x.p, c, P.clo.xyz);
    if (q.w <= 0.0) { continue; }
    let e = cacheE(q.idx, x.n);
    Ea += q.w * e; wa += q.w;
    if (cache[4u * q.idx].w > 0.0) { continue; }
    Ef += q.w * e; wf += q.w;
    if (dot(q.pos - x.p, x.ng) <= 0.0) { continue; }
    El += q.w * e; wl += q.w;
    let w = q.w * chebVis(q.idx, q.pos, at);
    if (w > 0.0) { E += w * e; ws += w; }
  }
  if (ws > 0.0) { return E / ws; }
  if ((P.wo.w >> 1u) >= 2u) { let g = gatherDepthSeen(x); if (g.w > 0.0 || (P.wo.w >> 1u) < 4u) { return g.xyz; } return gatherSeen(x); } // cg 4: then segments
  if ((P.wo.w >> 1u) == 1u) { return gatherSeen(x); }
  if (wl > 0.0) { return El / wl; }
  if (wf > 0.0) { return Ef / wf; }
  return Ea / wa;
}
fn surfaceRadiance(x: Surf, d: vec3f, useCache: bool) -> vec3f {
  let em = emission(x.slot);
  if (dot(x.n, d) >= 0.0) { return em; }
  var E = P.sunE.xyz * sunScale(x);
  if (useCache) { E += gatherCache(x); }
  return em + albedo(x.slot) / PI * E;
}
`;

// Level 1's merged radiance at a probe (bilinear in the face), and the level-0 probe under a level-l
// probe; for passes that bind level 1 as `next`.
export const LOOKUP = /* wgsl */ `
fn levelZeroIndex(l: u32, idx: u32) -> i32 { // world.mjs levelZeroIndex
  let n = P.dims[l]; let n0 = P.dims[0]; let s = 1u << l;
  let x = (idx % n.x) * s; let y = ((idx / n.x) % n.y) * s; let z = (idx / (n.x * n.y)) * s;
  if (x >= n0.x || y >= n0.y || z >= n0.z) { return -1; }
  return i32((z * n0.y + y) * n0.x + x);
}
// Level 1's merged radiance at probe q, bilinear in the face; w: the start visibility (1 without sv),
// which also weighs the bilinear taps (cascades3d.mjs lookup).
fn lookup(q: u32, d: vec3f) -> vec4f {
  let c = u32(P.geo[1].y); let tex = P.ofs[1].z; let F = faceOf(d); let cf = f32(c); let sv = P.wo.y != 0u;
  if (c == 1u) { let e = next[q * tex + F.f]; return vec4f(e.xyz, select(1.0, e.w, sv)); }
  let u = clamp((F.s + 1.0) / 2.0 * cf - 0.5, 0.0, cf - 1.0); let v = clamp((F.t + 1.0) / 2.0 * cf - 0.5, 0.0, cf - 1.0);
  let i0 = min(c - 2u, u32(floor(u))); let j0 = min(c - 2u, u32(floor(v))); let fu = u - f32(i0); let fv = v - f32(j0);
  let b = q * tex + F.f * c * c + j0 * c + i0;
  var out = vec3f(0.0); var ov = vec3f(0.0); var wv = 0.0;
  for (var t = 0u; t < 4u; t++) {
    let e = next[b + (t & 1u) + (t >> 1u) * c];
    let w = select(1.0 - fu, fu, (t & 1u) == 1u) * select(1.0 - fv, fv, (t >> 1u) == 1u);
    out += w * e.xyz; ov += w * e.w * e.xyz; wv += w * e.w;
  }
  if (!sv) { return vec4f(out, 1.0); }
  return select(vec4f(out, 0.0), vec4f(ov / wv, wv), wv > 0.0);
}
`;
