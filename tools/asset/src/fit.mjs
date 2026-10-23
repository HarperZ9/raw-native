// Landmark fit: damped Gauss-Newton (Levenberg-Marquardt) over named stroke parameters until the skin surface passes within `tol`
// of every landmark, with a trust penalty toward the starting values and a crease check on the result. Own derivation, finite-
// difference Jacobian. The residual of a landmark is the field value there, which is the distance to second order near the surface.
import { Field, project } from "./sdf.mjs";
import { extract } from "./extract.mjs";

const SKIN = new Set(["hair", "stache"]), KEYS = ["c", "a", "b", "r", "R", "h"];
export function paramsOf(strokes, free) {
  const ps = [];
  strokes.forEach((s, si) => { if (!free.has(s.id)) return; for (const k of KEYS) { const v = s[k]; if (Array.isArray(v)) v.forEach((_, i) => ps.push({ si, k, i })); else if (typeof v === "number") ps.push({ si, k, i: -1 }); } });
  return ps;
}
const get = (S, p) => (p.i < 0 ? S[p.si][p.k] : S[p.si][p.k][p.i]);
const set = (S, p, v) => { if (p.i < 0) S[p.si][p.k] = v; else S[p.si][p.k][p.i] = v; };
function solve(A, b) {   // Gaussian elimination with partial pivoting
  const n = b.length, M = A.map((r, i) => [...r, b[i]]);
  for (let c = 0; c < n; c++) {
    let m = c; for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[m][c])) m = r;
    [M[c], M[m]] = [M[m], M[c]]; const d = M[c][c] || 1e-12;
    for (let r = c + 1; r < n; r++) { const f = M[r][c] / d; for (let k = c; k <= n; k++) M[r][k] -= f * M[c][k]; }
  }
  const x = new Array(n).fill(0);
  for (let r = n - 1; r >= 0; r--) { let s = M[r][n]; for (let k = r + 1; k < n; k++) s -= M[r][k] * x[k]; x[r] = s / (M[r][r] || 1e-12); }
  return x;
}
export const skinField = (strokes, cull = false) => new Field(strokes, { exclude: SKIN, cull });   // no culling: a far landmark must see the true field, not 1e3
// distance from each landmark to the zero set: Newton projection of the landmark onto the surface, metres
export function landmarkErrors(strokes, landmarks) {
  const F = skinField(strokes);
  return landmarks.map((l) => { const q = project(F, l.p, 12, 0.02); return { id: l.id, err: Math.hypot(q[0] - l.p[0], q[1] - l.p[1], q[2] - l.p[2]) }; });
}
// crease measure: the largest angle between the normals of two neighbouring vertices of a 4 mm extraction, degrees, and the p99
export function creaseOf(strokes, cell = 0.004) {
  const F = skinField(strokes, true), B = F.bounds(0.004), M = extract(F, { lo: B.lo, hi: B.hi, cell }), N = M.nrm, a = [];
  for (let q = 0; q < M.quads.length; q += 4) for (let e = 0; e < 4; e++) {
    const u = M.quads[q + e], v = M.quads[q + (e + 1) % 4];
    a.push(Math.acos(Math.max(-1, Math.min(1, N[u * 3] * N[v * 3] + N[u * 3 + 1] * N[v * 3 + 1] + N[u * 3 + 2] * N[v * 3 + 2]))) * 180 / Math.PI);
  }
  a.sort((x, y) => x - y);
  return { max: a[a.length - 1], p99: a[Math.floor(0.99 * (a.length - 1))] };
}
export function fitLandmarks(strokes0, landmarks, { free, tol = 0.002, iters = 40, lambda = 1, scale = 0.01, creaseSlack = 15, rMin = 0.6, rMax = 1.6 } = {}) {
  const ps = paramsOf(strokes0, new Set(free)), x0 = ps.map((p) => get(strokes0, p)), before = landmarkErrors(strokes0, landmarks), c0 = creaseOf(strokes0);
  let lam = lambda, best = null;
  for (let attempt = 0; attempt < 4; attempt++, lam *= 8) {
    const S = JSON.parse(JSON.stringify(strokes0)), x = [...x0], w = lam * 1e-6 / (scale * scale);   // trust weight per parameter
    const res = () => { const F = skinField(S); return landmarks.map((l) => F.d(l.p[0], l.p[1], l.p[2])); };
    const cost = (rr, xx) => rr.reduce((s, v) => s + v * v, 0) + xx.reduce((s, v, i) => s + w * (v - x0[i]) ** 2, 0);
    let r = res(), cur = cost(r, x), mu = 1e-6;
    for (let it = 0; it < iters; it++) {
      const J = ps.map((p, j) => { const h = 2e-5, v = x[j]; set(S, p, v + h); const rp = res(); set(S, p, v); return rp.map((q, i) => (q - r[i]) / h); });
      const n = ps.length, A = Array.from({ length: n }, (_, a) => Array.from({ length: n }, (_, b) => J[a].reduce((s, v, i) => s + v * J[b][i], 0) + (a === b ? mu + w : 0)));
      const g = J.map((col, a) => col.reduce((s, v, i) => s + v * r[i], 0) + w * (x[a] - x0[a])), d = solve(A, g.map((v) => -v));
      const xn = x.map((v, j) => {
        const p = ps[j], lo = p.k === "r" ? x0[j] * rMin : -Infinity, hi = p.k === "r" ? x0[j] * rMax : Infinity;
        return Math.min(hi, Math.max(lo, v + Math.max(-0.01, Math.min(0.01, d[j]))));
      });
      ps.forEach((p, j) => set(S, p, xn[j])); const rn = res(), cn = cost(rn, xn);
      if (cn < cur) { const done = cur - cn < 1e-13; for (let j = 0; j < n; j++) x[j] = xn[j]; r = rn; cur = cn; mu = Math.max(1e-9, mu / 3); if (done) break; }
      else { ps.forEach((p, j) => set(S, p, x[j])); mu *= 5; }
    }
    ps.forEach((p, j) => set(S, p, x[j]));
    const after = landmarkErrors(S, landmarks), cr = creaseOf(S), ok = cr.p99 <= c0.p99 + creaseSlack;
    best = { strokes: S, before, after, crease: { before: c0, after: cr, ok }, tolMet: after.every((e) => e.err <= tol), attempts: attempt + 1 };
    if (ok) break;
  }
  return best;
}
