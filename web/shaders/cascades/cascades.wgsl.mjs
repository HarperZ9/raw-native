// WGSL for the radiance cascades pass: trace.mjs and cascades.mjs, step for step, in f32.
// Modules are TRACE + MERGE + one of the mains; each main declares the bindings it uses.
export const PARAMS_WORDS = 48;

export const PARAMS_STRUCT = /* wgsl */ `
struct Params {
  W: u32, H: u32, nCirc: u32, branch: u32,
  n: u32, pw: u32, ph: u32, s: u32,
  t0: f32, t1: f32, hasNext: u32, fix: u32,
  nn: u32, npw: u32, nph: u32, ns: u32,
  glass: u32, rays0: u32, rays1: u32, raysTotal: u32,
  seed: u32, stride: u32, gw: u32, gh: u32,
  fresnel: u32, total: u32, gridX: u32, level: u32,
  pad0: u32, pad1: u32, pad2: u32, pad3: u32,
  mipW: array<u32, 8>, mipOff: array<u32, 8>,
}
`;

export const TRACE_WGSL = PARAMS_STRUCT + /* wgsl */ `
@group(0) @binding(0) var<storage, read> P: Params;
@group(0) @binding(1) var<storage, read> grid: array<vec4f>;
@group(0) @binding(2) var<storage, read> mip: array<u32>;
@group(0) @binding(3) var<storage, read> circ: array<vec4f>;
const TAU = 6.283185307179586;
const MIPS = 8u;
struct Acc { L: vec3f, T: f32, out: bool }
struct Seg { L: vec3f, T: f32, p: vec2f, d: vec2f, out: bool, bent: bool }

// Square root with one Newton step: WGSL lets an adapter compute sqrt as 1 / inverseSqrt, which
// leaves relative errors that bend refracted rays visibly against the f64 reference.
fn sqrtR(x: f32) -> f32 { if (x <= 0.0) { return 0.0; } let r = sqrt(x); return 0.5 * (r + x / r); }
fn occ(l: u32, x: i32, y: i32) -> bool { return mip[P.mipOff[l] + u32(y >> l) * P.mipW[l] + u32(x >> l)] != 0u; }
fn exitT(b: f32, p: f32, iv: f32, nz: bool) -> f32 { return select(1e30, (b - p) * iv, nz); }

fn march(Pp: vec2f, d: vec2f, len: f32, acc: ptr<function, Acc>) {
  let W = i32(P.W); let H = i32(P.H);
  if (len <= 0.0) { return; }
  if (Pp.x < 0.0 || Pp.y < 0.0 || Pp.x > f32(W) || Pp.y > f32(H)) { (*acc).out = true; return; }
  let sx = select(-1, 1, d.x > 0.0); let sy = select(-1, 1, d.y > 0.0);
  let nzx = d.x != 0.0; let nzy = d.y != 0.0;
  let ivx = select(0.0, 1.0 / d.x, nzx); let ivy = select(0.0, 1.0 / d.y, nzy);
  var ix = min(W - 1, i32(floor(Pp.x))); var iy = min(H - 1, i32(floor(Pp.y))); var t = 0.0;
  for (var guard = 0u; guard < 1048576u; guard++) {
    if (ix < 0 || iy < 0 || ix >= W || iy >= H) { (*acc).out = true; return; }
    if (t >= len || (*acc).T < 1e-6) { return; }
    if (!occ(0u, ix, iy)) {
      var l = 0u;
      while (l + 1u < MIPS && !occ(l + 1u, ix, iy)) { l++; }
      let size = 1 << l; let bx = (ix >> l) << l; let by = (iy >> l) << l;
      let tx = exitT(f32(select(bx, bx + size, sx > 0)), Pp.x, ivx, nzx);
      let ty = exitT(f32(select(by, by + size, sy > 0)), Pp.y, ivy, nzy);
      if (min(tx, ty) >= len) { return; }
      if (tx < ty) { t = max(t, tx); ix = select(bx - 1, bx + size, sx > 0); iy = clamp(i32(floor(Pp.y + d.y * t)), by, by + size - 1); }
      else { t = max(t, ty); iy = select(by - 1, by + size, sy > 0); ix = clamp(i32(floor(Pp.x + d.x * t)), bx, bx + size - 1); }
      continue;
    }
    let tx = exitT(f32(select(ix, ix + 1, sx > 0)), Pp.x, ivx, nzx);
    let ty = exitT(f32(select(iy, iy + 1, sy > 0)), Pp.y, ivy, nzy);
    let tn = min(min(tx, ty), len); let seg = tn - t;
    if (seg > 0.0) {
      let g = grid[u32(iy) * P.W + u32(ix)]; let a = exp(-g.w * seg);
      (*acc).L += (*acc).T * (1.0 - a) * g.xyz; (*acc).T *= a;
    }
    t = max(t, tn);
    if (tn >= len) { return; }
    if (tx < ty) { ix += sx; } else { iy += sy; }
  }
}

fn hitCircle(p: vec2f, d: vec2f, last: i32, inside: bool) -> vec2f { // (t, index); t = 1e30 when nothing is hit
  var best = 1e30; var bi = -1.0;
  for (var i = 0u; i < P.nCirc; i++) {
    let c = circ[2u * i]; let o = p - c.xy; let b = dot(o, d); let disc = b * b - (dot(o, o) - c.z * c.z);
    var t = 1e30;
    if (i32(i) == last) { if (!inside) { continue; } t = -b + sqrtR(disc); }
    else {
      if (disc < 0.0) { continue; }
      let q = sqrtR(disc); let t1 = -b - q; let t2 = -b + q;
      t = select(select(1e30, t2, t2 > 1e-4), t1, t1 > 1e-4);
    }
    if (t < best) { best = t; bi = f32(i); }
  }
  return vec2f(best, bi);
}
fn insideEmitter(p: vec2f) -> i32 {
  for (var i = 0u; i < P.nCirc; i++) {
    let c = circ[2u * i]; let o = p - c.xy;
    if (c.w == 0.0 && dot(o, o) < c.z * c.z) { return i32(i); }
  }
  return -1;
}
fn scatterGlass(ci: u32, p: vec2f, d: vec2f, fres: bool, u: f32) -> vec2f {
  let c = circ[2u * ci]; let eta0 = circ[2u * ci + 1u].x;
  var n = (p - c.xy) / c.z; var cosi = -dot(n, d); var n1 = 1.0; var n2 = eta0;
  if (cosi < 0.0) { n = -n; cosi = -cosi; n1 = eta0; n2 = 1.0; }
  let eta = n1 / n2; let k = 1.0 - eta * eta * (1.0 - cosi * cosi);
  let refl = d + 2.0 * cosi * n;
  if (k < 0.0) { return refl; }
  let cost = sqrtR(k);
  if (fres) {
    let rs = (n1 * cosi - n2 * cost) / (n1 * cosi + n2 * cost); let rp = (n2 * cosi - n1 * cost) / (n2 * cosi + n1 * cost);
    if (u < 0.5 * (rs * rs + rp * rp)) { return refl; }
  }
  let v = eta * d + (eta * cosi - cost) * n; return v / sqrtR(dot(v, v));
}
fn hash32(x0: u32) -> u32 {
  let x = x0 * 747796405u + 2891336453u; let w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  return (w >> 22u) ^ w;
}
fn uni(s: ptr<function, u32>) -> f32 { *s = hash32(*s); return f32(*s) / 4294967296.0; }

fn traceInterval(o: vec2f, d0: vec2f, t0: f32, t1: f32, rng: ptr<function, u32>, useRng: bool) -> Seg {
  var acc = Acc(vec3f(0.0), 1.0, false);
  var p = o; var dir = d0; var s = 0.0; var started = false; var bent = false; var last = -1; var inside = false;
  for (var ev = 0u; ev < 32u; ev++) {
    var th = 1e30; var hi = -1;
    if (P.nCirc > 0u) { let h = hitCircle(p, dir, last, inside); th = h.x; hi = i32(h.y); }
    let end = min(th, t1 - s); let a0 = max(0.0, t0 - s);
    if (!started && end >= a0) {
      started = true;
      let ps = p + dir * a0; let ce = insideEmitter(ps);
      if (ce >= 0) { acc.L += acc.T * circ[2u * u32(ce) + 1u].xyz; return Seg(acc.L, 0.0, ps, dir, false, bent); }
    }
    if (end > a0) { march(p + dir * a0, dir, end - a0, &acc); }
    if (acc.out || acc.T < 1e-6 || th >= t1 - s) {
      return Seg(acc.L, acc.T, p + dir * min(th, t1 - s), dir, acc.out, bent);
    }
    p += dir * th; s += th;
    let c = circ[2u * u32(hi)];
    if (c.w == 0.0 && s >= t0) { acc.L += acc.T * circ[2u * u32(hi) + 1u].xyz; return Seg(acc.L, 0.0, p, dir, false, bent); }
    if (c.w != 0.0) { var u = 0.0; if (useRng) { u = uni(rng); } dir = scatterGlass(u32(hi), p, dir, useRng, u); bent = true; }
    last = hi; inside = dot(p - c.xy, dir) < 0.0;
  }
  return Seg(acc.L, acc.T, p, dir, acc.out, bent);
}
fn flatIndex(gid: vec3u) -> u32 { return gid.y * P.gridX + gid.x; }
// cos and sin of th in [0, 2 pi) by quadrant reduction and Taylor polynomials on [-pi/4, pi/4]
// (error under 3e-8), so ray directions do not depend on the adapter's sin and cos, which WGSL bounds
// only to 2^-11 absolute (SwiftShader uses that room; the RTX 4090 does not).
fn dirOf(th: f32) -> vec2f {
  let q = round(th / 1.5707963267948966); let r = th - q * 1.5707963267948966; let r2 = r * r;
  let s = r * (1.0 + r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0 + r2 * (1.0 / 362880.0)))));
  let c = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 + r2 * (-1.0 / 720.0 + r2 * (1.0 / 40320.0))));
  let k = u32(i32(q) & 3);
  if (k == 0u) { return vec2f(c, s); }
  if (k == 1u) { return vec2f(-s, c); }
  if (k == 2u) { return vec2f(-c, -s); }
  return vec2f(s, -c);
}
`;

export const MERGE_WGSL = /* wgsl */ `
@group(0) @binding(4) var<storage, read> nxt: array<f32>;
fn rd3(i: u32) -> vec3f { return vec3f(nxt[3u * i], nxt[3u * i + 1u], nxt[3u * i + 2u]); }
fn wrapK(a: i32, n: i32) -> u32 { return u32(((a % n) + n) % n); }
fn sampleNext(q: vec2f, th: f32) -> vec3f {
  let pw = f32(P.npw); let ph = f32(P.nph); let s = f32(P.ns);
  let u = clamp(q.x / s - 0.5, 0.0, pw - 1.0); let v = clamp(q.y / s - 0.5, 0.0, ph - 1.0);
  let x0 = u32(floor(u)); let y0 = u32(floor(v)); let fx = u - f32(x0); let fy = v - f32(y0);
  let x1 = min(x0 + 1u, P.npw - 1u); let y1 = min(y0 + 1u, P.nph - 1u);
  let a = th / TAU * f32(P.nn) - 0.5 - f32(P.branch - 1u) * 0.5;
  var out = vec3f(0.0);
  let xs = array<u32, 4>(x0, x1, x0, x1); let ys = array<u32, 4>(y0, y0, y1, y1);
  let ws = array<f32, 4>((1.0 - fx) * (1.0 - fy), fx * (1.0 - fy), (1.0 - fx) * fy, fx * fy);
  for (var c = 0u; c < 4u; c++) {
    if (ws[c] == 0.0) { continue; }
    let base = (ys[c] * P.npw + xs[c]) * P.nn; let ww = ws[c] / f32(P.branch);
    for (var j = 0u; j < P.branch; j++) {
      let aj = a + f32(j); let k0 = i32(floor(aj)); let f = aj - f32(k0);
      out += ww * ((1.0 - f) * rd3(base + wrapK(k0, i32(P.nn))) + f * rd3(base + wrapK(k0 + 1, i32(P.nn))));
    }
  }
  return out;
}
// The angle of a unit vector in [0, 2 pi): a polynomial arctangent on the first octant, then two
// Newton steps on tan(a - a0) with dirOf, so the result does not rest on the adapter's atan2, which
// WGSL bounds loosely.
fn angleOf(d: vec2f) -> f32 {
  let ax = abs(d.x); let ay = abs(d.y); let m = min(ax, ay) / max(max(ax, ay), 1e-30); let m2 = m * m;
  var a = m * (0.99997726 + m2 * (-0.33262347 + m2 * (0.19354346 + m2 * (-0.11643287 + m2 * (0.05265332 + m2 * -0.01172120)))));
  if (ay > ax) { a = 1.5707963267948966 - a; }
  if (d.x < 0.0) { a = 3.141592653589793 - a; }
  if (d.y < 0.0) { a = TAU - a; }
  for (var it = 0; it < 2; it++) {
    let c = dirOf(clamp(a, 0.0, TAU));
    a += (c.x * d.y - c.y * d.x) / (c.x * d.x + c.y * d.y);
  }
  return select(select(a, a - TAU, a >= TAU), a + TAU, a < 0.0);
}
fn entry(px: u32, py: u32, k: u32) -> vec3f {
  let th = (f32(k) + 0.5) * TAU / f32(P.n); let s = f32(P.s);
  let p = (vec2f(f32(px), f32(py)) + 0.5) * s; let d = dirOf(th);
  var rng = 0u;
  if (P.fix != 0u && P.hasNext != 0u && P.glass == 0u) {
    let ns = f32(P.ns);
    let u = clamp(p.x / ns - 0.5, 0.0, f32(P.npw) - 1.0); let v = clamp(p.y / ns - 0.5, 0.0, f32(P.nph) - 1.0);
    let x0 = u32(floor(u)); let y0 = u32(floor(v)); let fx = u - f32(x0); let fy = v - f32(y0);
    let start = p + d * P.t0; var out = vec3f(0.0);
    let xs = array<u32, 4>(x0, x0 + 1u, x0, x0 + 1u); let ys = array<u32, 4>(y0, y0, y0 + 1u, y0 + 1u);
    let ws = array<f32, 4>((1.0 - fx) * (1.0 - fy), fx * (1.0 - fy), (1.0 - fx) * fy, fx * fy);
    for (var c = 0u; c < 4u; c++) {
      if (ws[c] == 0.0) { continue; }
      let q = (vec2f(f32(min(xs[c], P.npw - 1u)), f32(min(ys[c], P.nph - 1u))) + 0.5) * ns;
      let e = q + d * P.t1 - start; let len = sqrtR(dot(e, e));
      let r = traceInterval(start, e / len, 0.0, len, &rng, false);
      var M = vec3f(0.0);
      if (!(r.out || r.T < 1e-6)) { M = sampleNext(q, th); }
      out += ws[c] * (r.L + r.T * M);
    }
    return out;
  }
  let r = traceInterval(p, d, P.t0, P.t1, &rng, false);
  if (P.hasNext == 0u || r.out || r.T < 1e-6) { return r.L; }
  var q = p; var a = th;
  if (r.bent) { q = r.p - r.d * P.t1; a = angleOf(r.d); }
  return r.L + r.T * sampleNext(q, a);
}
`;

export const LEVEL_MAIN = /* wgsl */ `
@group(0) @binding(5) var<storage, read_write> cur: array<f32>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = flatIndex(gid); if (i >= P.total) { return; }
  let k = i % P.n; let pr = i / P.n; let L = entry(pr % P.pw, pr / P.pw, k);
  cur[3u * i] = L.x; cur[3u * i + 1u] = L.y; cur[3u * i + 2u] = L.z;
}`;

export const FINAL_MAIN = /* wgsl */ `
@group(0) @binding(5) var<storage, read_write> outF: array<f32>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = flatIndex(gid); if (i >= P.total) { return; }
  var F = vec3f(0.0);
  for (var k = 0u; k < P.n; k++) { F += entry(i % P.W, i / P.W, k); }
  F *= TAU / f32(P.n);
  outF[3u * i] = F.x; outF[3u * i + 1u] = F.y; outF[3u * i + 2u] = F.z;
}`;

// Brute force: rays [rays0, rays1) of raysTotal per sample, accumulated into refF (cleared by the host).
export const REF_MAIN = /* wgsl */ `
@group(0) @binding(4) var<storage, read_write> refF: array<f32>;
@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid: vec3u) {
  let s = flatIndex(gid); if (s >= P.total) { return; }
  let off = f32(P.stride / 2u);
  let p = vec2f(f32(P.stride * (s % P.gw)) + off + 0.5, f32(P.stride * (s / P.gw)) + off + 0.5);
  let big = 4.0 * length(vec2f(f32(P.W), f32(P.H)));
  var acc = vec3f(0.0);
  for (var r = P.rays0; r < P.rays1; r++) {
    var st = hash32(hash32(s + P.seed * 0x9e3779b9u) + r);
    let th = (f32(r) + uni(&st)) * TAU / f32(P.raysTotal);
    let t = traceInterval(p, dirOf(th), 0.0, big, &st, P.fresnel != 0u);
    acc += t.L;
  }
  refF[3u * s] += acc.x; refF[3u * s + 1u] += acc.y; refF[3u * s + 2u] += acc.z;
}`;

// Occupancy pyramid: level 0 from the grid, level l from level l-1.
export const MIP_MAIN = PARAMS_STRUCT + /* wgsl */ `
@group(0) @binding(0) var<storage, read> P: Params;
@group(0) @binding(1) var<storage, read> grid: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> mip: array<u32>;
@compute @workgroup_size(16, 16) fn main(@builtin(global_invocation_id) gid: vec3u) {
  let l = P.level; let w = P.mipW[l]; let h = (P.H + (1u << l) - 1u) >> l;
  if (gid.x >= w || gid.y >= h) { return; }
  var o = 0u;
  if (l == 0u) { o = select(0u, 1u, grid[gid.y * P.W + gid.x].w > 0.0); }
  else {
    let pw = P.mipW[l - 1u]; let ph = (P.H + (1u << (l - 1u)) - 1u) >> (l - 1u);
    for (var j = 0u; j < 2u; j++) { for (var i = 0u; i < 2u; i++) {
      let x = 2u * gid.x + i; let y = 2u * gid.y + j;
      if (x < pw && y < ph) { o = max(o, mip[P.mipOff[l - 1u] + y * pw + x]); }
    } }
  }
  mip[P.mipOff[l] + gid.y * w + gid.x] = o;
}`;
