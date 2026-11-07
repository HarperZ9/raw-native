// Shared math of the 3D radiance cascades (float64 here; wgsl3d.mjs repeats it in f32).
// Directions are cube-map texels: level i has c_i = c0 * 2^i texels a face side, 6 c_i^2 in all, and
// texel (i, j) of a face splits into the 2 x 2 texels (2i..2i+1, 2j..2j+1) of the next level, so a
// texel's children cover exactly its solid angle. Solid angles are exact (omegaTable).

export const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
export const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
export const norm = (a) => { const m = Math.hypot(a[0], a[1], a[2]); return m > 0 ? [a[0] / m, a[1] / m, a[2] / m] : [0, 0, 0]; };

// Texel k of a level with face side c: its unit direction (the texel centre on the cube).
export function texelDir(k, c) {
  const f = Math.floor(k / (c * c)), r = k - f * c * c, j = Math.floor(r / c), i = r - j * c;
  const m = f >> 1, sg = f & 1 ? -1 : 1, d = [0, 0, 0];
  d[m] = sg; d[(m + 1) % 3] = (i + 0.5) / c * 2 - 1; d[(m + 2) % 3] = (j + 0.5) / c * 2 - 1;
  return norm(d);
}
// Face and face coordinates (s, t in [-1, 1]) of a direction. Ties go to the lower axis.
export function faceOf(d) {
  let m = 0;
  if (Math.abs(d[1]) > Math.abs(d[m])) m = 1;
  if (Math.abs(d[2]) > Math.abs(d[m])) m = 2;
  const a = Math.abs(d[m]);
  return { f: 2 * m + (d[m] < 0 ? 1 : 0), s: d[(m + 1) % 3] / a, t: d[(m + 2) % 3] / a };
}
export function texelOf(d, c) {
  const { f, s, t } = faceOf(d);
  const i = Math.min(c - 1, Math.max(0, Math.floor((s + 1) / 2 * c))), j = Math.min(c - 1, Math.max(0, Math.floor((t + 1) / 2 * c)));
  return f * c * c + j * c + i;
}
// Solid angle of each texel of one face side c (the same on every face), exact.
export function omegaTable(c) {
  const A = (x, y) => Math.atan2(x * y, Math.sqrt(x * x + y * y + 1)), o = new Float64Array(c * c);
  for (let j = 0; j < c; j++) for (let i = 0; i < c; i++) {
    const x0 = i / c * 2 - 1, x1 = (i + 1) / c * 2 - 1, y0 = j / c * 2 - 1, y1 = (j + 1) / c * 2 - 1;
    o[j * c + i] = A(x1, y1) - A(x0, y1) - A(x1, y0) + A(x0, y0);
  }
  return o;
}

// PCG-style hash, identical in WGSL.
export function hash32(x) {
  x = (Math.imul(x >>> 0, 747796405) + 2891336453) >>> 0;
  const w = Math.imul(((x >>> ((x >>> 28) + 4)) ^ x) >>> 0, 277803737) >>> 0;
  return ((w >>> 22) ^ w) >>> 0;
}
// Orthonormal basis around n (Duff et al. 2017).
export function basis(n) {
  const sg = n[2] >= 0 ? 1 : -1, a = -1 / (sg + n[2]), b = n[0] * n[1] * a;
  return [[1 + sg * n[0] * n[0] * a, sg * b, -sg * n[0]], [b, sg + n[1] * n[1] * a, -n[1]]];
}
// Stratum (i, j) of m x m, jittered by (h1, h2), mapped to a cosine-weighted direction around n
// (Shirley-Chiu concentric disk, then lifted to the hemisphere).
export function cosineDir(n, i, j, m, h1, h2) {
  const a = 2 * (i + h1) / m - 1, b = 2 * (j + h2) / m - 1;
  let r = 0, phi = 0;
  if (a !== 0 || b !== 0) {
    if (Math.abs(a) > Math.abs(b)) { r = a; phi = Math.PI / 4 * (b / a); } else { r = b; phi = Math.PI / 2 - Math.PI / 4 * (a / b); }
  }
  const x = r * Math.cos(phi), y = r * Math.sin(phi), z = Math.sqrt(Math.max(0, 1 - x * x - y * y)), [t, bt] = basis(n);
  return norm([t[0] * x + bt[0] * y + n[0] * z, t[1] * x + bt[1] * y + n[1] * z, t[2] * x + bt[2] * y + n[2] * z]);
}
// The jitter of stratum k of screen probe p: two uniforms from the hash.
export function jitter(p, k) {
  const h = hash32((Math.imul(p >>> 0, 0x9e3779b9) + k) >>> 0), g = hash32(h);
  return [h / 4294967296, g / 4294967296];
}

// The plan: world levels over the scene's bounds. Level i: probe spacing h0 2^i on a grid that starts
// half a spacing below the bounds (so probes of level i+1 sit on every other probe of level i), face
// side c0 2^i, interval [T_i, T_i+1] with T_i = r0 h0 (f^i - 1) / (f - 1); the top level's interval
// runs to infinity. Options: stride 0 gathers every pixel from level 0 ("world"); stride k >= 1 adds
// screen probes every k pixels with m x m cosine directions, merged into level 1. fix: "none", "probe"
// or "full" (cascades3d.mjs).
export function planCascades3d(bounds, { h0 = 0.5, c0 = 1, r0 = 2, f = 2, levels = 0, fix = "probe", ov = 1, stride = 4, m = 8, bounces = 8, cd = 4,
  tf = 0, tmax = 32, sf = 0, sl = 0, move = null, c1 = 0, mw = "seg", sv = 0, fz = 0, ft = 0, cg = 0, jit = 0, jl = 1, go = null } = {}) {
  // c1 (0 = off): level 1's face side, c0 times a power of two above c0; levels above keep at least
  // c1, so a parent texel splits into (c_i+1 / c_i)^2 children. mw: how screen probes weigh level 1's
  // neighbours ("seg" the segment test, "vis" depth-moment visibility); sv 1 weighs every merge by
  // the start visibility of the neighbour's texels (world.mjs).
  // go: a fixed offset of the whole grid (metres), for testing how results depend on its placement.
  // jit K > 0: K phases of a world-grid offset of up to h0 2^jl a frame (world.mjs phasePlan).
  if (c1 && !(c1 > c0 && Number.isInteger(Math.log2(c1 / c0)))) throw new Error(`c1 ${c1}: c0 times a power of two above c0`);
  if (!["seg", "vis"].includes(mw)) throw new Error(`mw ${mw}`);
  if (jit && (jit % 2 || !tf)) throw new Error("jit needs an even phase count and temporal accumulation (tf)");
  // The grid starts half a spacing (plus an odd fraction) below the bounds: probes then never sit on a
  // box's face or on the diagonal of a face's two triangles, where f32 and f64 ray tests disagree. With
  // jit it starts one jitter span lower, so every phase's grid still covers the bounds.
  const hj = jit ? h0 * 2 ** jl : 0;
  const lo = bounds.lo.map((v, k) => v - h0 * (0.5 + [0.0137, 0.0291, 0.0419][k]) - hj + (go ? go[k] : 0)), ext = bounds.hi.map((v, k) => v - lo[k]);
  const diag = Math.hypot(...ext), T = (i) => r0 * h0 * (f ** i - 1) / (f - 1), L = [];
  for (let i = 0; ; i++) {
    const h = h0 * 2 ** i, c = i && c1 ? Math.max(c0 * 2 ** i, c1) : c0 * 2 ** i, n = ext.map((e) => Math.ceil(e / h) + 1);
    L.push({ i, h, c, n, probes: n[0] * n[1] * n[2], texels: 6 * c * c, T0: T(i), T1: T(i + 1) });
    if ((levels && L.length === levels) || (!levels && (T(i + 1) >= diag || L.length === 8))) break;
  }
  L[L.length - 1].T1 = Infinity;
  // fix "probe" also extends each interval past T_i+1 by ov x sqrt(3) x the next spacing, the farthest a
  // next-level neighbour sits, so a surface in the gap between this interval's end and a neighbour's
  // interval start is met here instead of skipped (the merge's parallax).
  L.forEach((l, i) => { l.T1x = i + 1 < L.length && fix === "probe" ? l.T1 + ov * Math.sqrt(3) * L[i + 1].h : l.T1; });
  // A frame adds one bounce: with screen probes the pixel holds B bounces after B - 1 frames.
  // Denoise (denoise3d.mjs): tf > 0 adds tf frames of temporal accumulation (at most tmax frames of
  // history) after the warm-up; sf a-trous passes over the screen probes, sl their luminance edge stop.
  const warm = stride ? Math.max(1, bounces - 1) : bounces, frames = warm + (stride ? tf : 0);
  return { lo, levels: L, fix, ov, stride, m, bounces, frames, warm, tf: stride ? tf : 0, tmax, sf: stride ? sf : 0, sl, move,
    h0, c0, r0, f, cd, dmax: 2 * Math.sqrt(3) * h0, omega: L.map((l) => omegaTable(l.c)), c1, mw, sv, fz, ft, cg, jit: stride ? jit : 0, jl, hj };
}
// The 8 neighbours of point p on level l: { idx, w } with trilinear weights (clamped to the grid).
export function neighbours(plan, l, p) {
  const L = plan.levels[l], g = [0, 1, 2].map((k) => Math.min(L.n[k] - 1, Math.max(0, (p[k] - plan.lo[k]) / L.h)));
  const b = g.map((v, k) => Math.min(L.n[k] - 2, Math.floor(v))), fr = g.map((v, k) => v - b[k]), out = [];
  for (let c = 0; c < 8; c++) {
    const o = [c & 1, (c >> 1) & 1, (c >> 2) & 1], q = o.map((v, k) => b[k] + v);
    const w = (o[0] ? fr[0] : 1 - fr[0]) * (o[1] ? fr[1] : 1 - fr[1]) * (o[2] ? fr[2] : 1 - fr[2]);
    out.push({ idx: (q[2] * L.n[1] + q[1]) * L.n[0] + q[0], w, pos: q.map((v, k) => plan.lo[k] + v * L.h) });
  }
  return out;
}
export function probePos(plan, l, idx) {
  const L = plan.levels[l], x = idx % L.n[0], y = Math.floor(idx / L.n[0]) % L.n[1], z = Math.floor(idx / (L.n[0] * L.n[1]));
  return [plan.lo[0] + x * L.h, plan.lo[1] + y * L.h, plan.lo[2] + z * L.h];
}
