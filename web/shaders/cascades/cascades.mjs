// Radiance cascades in flatland, CPU reference (float64), and the brute-force fluence it is measured
// against. Method: A. Sannikov, "Radiance Cascades: A Novel Approach to Calculating Global
// Illumination" (working paper); written here from the paper's description, no code taken.
//
//   const plan = planCascades(W, H, { branch: 2, n0: 4, r0: 1, fix: false });
//   const F = runCascades(scene, plan);           // Float64Array W*H*3, fluence per pixel
//   const R = referenceFluence(scene, { rays: 4096, stride: 1, seed: 1 });
//
// Cascade i has probes every 2^i pixels, n0 * branch^i directions, and holds the radiance interval
// [t_i, t_i+1] with t_i = r0 (branch^i - 1) / (branch - 1). Branch 2 is the paper's scaling (memory
// halves per cascade); branch 4 keeps memory constant per cascade (the paper's Radiant 2d note).
// Cascades merge top-down (paper eq. 13): L_i + beta_i * L_i+1, where L_i+1 is cascade i+1
// interpolated bilinearly in space at the probe and averaged over the branch child directions.
//
// Two additions, both measured against the reference:
// - fix: per-neighbour continuation. Each of the four bilinear neighbours in cascade i+1 gets its own
//   ray from this interval's start to that neighbour's interval start, so a thin wall between a
//   probe and its neighbour blocks what it should (the parallax the plain merge ignores).
// - bent rays: when a glass circle turns a ray, the continuation is read at the virtual probe
//   q = end - dir_end * t_i+1 in the end direction, which equals the plain merge for straight rays.
import { buildMips, traceInterval, hash32, makeRng } from "./trace.mjs";

const TAU = 2 * Math.PI;

export function planCascades(W, H, { branch = 2, n0 = 4, r0 = 1, fix = false } = {}) {
  const diag = Math.hypot(W, H), cascades = [];
  for (let i = 0; ; i++) {
    const bi = branch ** i, s = 2 ** i;
    const t0 = r0 * (bi - 1) / (branch - 1), t1 = r0 * (bi * branch - 1) / (branch - 1);
    cascades.push({ i, s, pw: Math.ceil(W / s), ph: Math.ceil(H / s), n: n0 * bi, t0, t1 });
    if (t1 >= diag) break;
  }
  return { W, H, branch, n0, r0, fix, cascades };
}

// Cascade i+1's merged radiance at position q (pixels) for a continuous direction angle th, averaged
// over the `branch` child bins centred on th. Straight rays land exactly on the child bins.
export function sampleNext(buf, cn, branch, q, th) {
  let u = q[0] / cn.s - 0.5, v = q[1] / cn.s - 0.5;
  u = Math.min(Math.max(u, 0), cn.pw - 1); v = Math.min(Math.max(v, 0), cn.ph - 1);
  const x0 = Math.floor(u), y0 = Math.floor(v), fx = u - x0, fy = v - y0;
  const x1 = Math.min(x0 + 1, cn.pw - 1), y1 = Math.min(y0 + 1, cn.ph - 1);
  const a = th / TAU * cn.n - 0.5 - (branch - 1) / 2, out = [0, 0, 0];
  const probes = [[x0, y0, (1 - fx) * (1 - fy)], [x1, y0, fx * (1 - fy)], [x0, y1, (1 - fx) * fy], [x1, y1, fx * fy]];
  for (const [px, py, w] of probes) {
    if (w === 0) continue;
    const base = (py * cn.pw + px) * cn.n;
    for (let j = 0; j < branch; j++) {
      const aj = a + j, k0 = Math.floor(aj), f = aj - k0;
      const i0 = 3 * (base + mod(k0, cn.n)), i1 = 3 * (base + mod(k0 + 1, cn.n)), ww = w / branch;
      for (let c = 0; c < 3; c++) out[c] += ww * ((1 - f) * buf[i0 + c] + f * buf[i1 + c]);
    }
  }
  return out;
}
function mod(a, n) { return ((a % n) + n) % n; }
function angleOf(d) { const a = Math.atan2(d[1], d[0]); return a < 0 ? a + TAU : a; }

// Merged radiance of one entry (probe px, py, direction k) of cascade i.
export function entry(sc, mips, plan, i, px, py, k, next) {
  const c = plan.cascades[i], cn = plan.cascades[i + 1], th = (k + 0.5) * TAU / c.n;
  const p = [(px + 0.5) * c.s, (py + 0.5) * c.s], d = [Math.cos(th), Math.sin(th)];
  const glass = sc.circles.some((g) => g.kind === "glass");
  if (plan.fix && next && !glass) {
    const u = Math.min(Math.max(p[0] / cn.s - 0.5, 0), cn.pw - 1), v = Math.min(Math.max(p[1] / cn.s - 0.5, 0), cn.ph - 1);
    const x0 = Math.floor(u), y0 = Math.floor(v), fx = u - x0, fy = v - y0, out = [0, 0, 0];
    const start = [p[0] + d[0] * c.t0, p[1] + d[1] * c.t0];
    for (const [qx, qy, w] of [[x0, y0, (1 - fx) * (1 - fy)], [x0 + 1, y0, fx * (1 - fy)], [x0, y0 + 1, (1 - fx) * fy], [x0 + 1, y0 + 1, fx * fy]]) {
      if (w === 0) continue;
      const qxc = Math.min(qx, cn.pw - 1), qyc = Math.min(qy, cn.ph - 1), q = [(qxc + 0.5) * cn.s, (qyc + 0.5) * cn.s];
      const e = [q[0] + d[0] * c.t1 - start[0], q[1] + d[1] * c.t1 - start[1]], len = Math.hypot(e[0], e[1]);
      const r = traceInterval(sc, mips, start, [e[0] / len, e[1] / len], 0, len);
      const M = r.out || r.T < 1e-6 ? [0, 0, 0] : sampleNext(next, cn, plan.branch, q, th);
      for (let ch = 0; ch < 3; ch++) out[ch] += w * (r.L[ch] + r.T * M[ch]);
    }
    return out;
  }
  const r = traceInterval(sc, mips, p, d, c.t0, c.t1);
  if (!next || r.out || r.T < 1e-6) return r.L;
  const q = r.bent ? [r.p[0] - r.d[0] * c.t1, r.p[1] - r.d[1] * c.t1] : p;
  const M = sampleNext(next, cn, plan.branch, q, r.bent ? angleOf(r.d) : th);
  return [r.L[0] + r.T * M[0], r.L[1] + r.T * M[1], r.L[2] + r.T * M[2]];
}

export function runCascades(sc, plan, mips = buildMips(sc.grid, sc.W, sc.H)) {
  let next = null;
  for (let i = plan.cascades.length - 1; i >= 1; i--) {
    const c = plan.cascades[i], cur = new Float64Array(c.pw * c.ph * c.n * 3);
    for (let py = 0; py < c.ph; py++) for (let px = 0; px < c.pw; px++) for (let k = 0; k < c.n; k++) {
      cur.set(entry(sc, mips, plan, i, px, py, k, next), 3 * ((py * c.pw + px) * c.n + k));
    }
    next = cur;
  }
  const c0 = plan.cascades[0], F = new Float64Array(sc.W * sc.H * 3), w = TAU / c0.n;
  for (let y = 0; y < sc.H; y++) for (let x = 0; x < sc.W; x++) for (let k = 0; k < c0.n; k++) {
    const L = entry(sc, mips, plan, 0, x, y, k, next), o = 3 * (y * sc.W + x);
    F[o] += w * L[0]; F[o + 1] += w * L[1]; F[o + 2] += w * L[2];
  }
  return F;
}

// Reference grid of a stride: sample (i, j) sits at pixel (stride i + stride/2, stride j + stride/2).
export function refGrid(W, H, stride) {
  return { stride, w: Math.floor(W / stride), h: Math.floor(H / stride), off: Math.floor(stride / 2) };
}
// The seed of ray j of reference sample s: identical on the GPU.
export function raySeed(seed, s, j) { return hash32((hash32((s + Math.imul(seed, 0x9e3779b9)) >>> 0) + j) >>> 0); }

// Brute force: `rays` stratified, jittered directions per sample, each traced to the end of the frame
// with Fresnel-chosen reflection at glass. Returns Float64Array w*h*3 of fluence.
export function referenceFluence(sc, { rays = 4096, stride = 1, seed = 1, fresnel = true } = {}, mips = buildMips(sc.grid, sc.W, sc.H)) {
  const g = refGrid(sc.W, sc.H, stride), out = new Float64Array(g.w * g.h * 3), big = 4 * Math.hypot(sc.W, sc.H);
  for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) {
    const s = j * g.w + i, p = [stride * i + g.off + 0.5, stride * j + g.off + 0.5], o = 3 * s;
    for (let r = 0; r < rays; r++) {
      const rng = makeRng(raySeed(seed, s, r)), th = (r + rng()) * TAU / rays;
      const t = traceInterval(sc, mips, p, [Math.cos(th), Math.sin(th)], 0, big, fresnel ? rng : null);
      out[o] += t.L[0]; out[o + 1] += t.L[1]; out[o + 2] += t.L[2];
    }
    for (let c = 0; c < 3; c++) out[o + c] *= TAU / rays;
  }
  return out;
}

// A full-resolution fluence image read at the reference grid's sample points.
export function atGrid(F, W, H, stride) {
  const g = refGrid(W, H, stride), out = new Float64Array(g.w * g.h * 3);
  for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) {
    const src = 3 * ((stride * j + g.off) * W + stride * i + g.off), dst = 3 * (j * g.w + i);
    out[dst] = F[src]; out[dst + 1] = F[src + 1]; out[dst + 2] = F[src + 2];
  }
  return out;
}
