// Curvature-adaptive density: quadric edge collapse (Garland and Heckbert, 1997, the standard method) on a triangle net,
// with the sculpt field as the judge. Flat regions collapse first because their quadric cost is zero; a collapse moves the
// vertex to the zero set (Newton) and is rejected when the field distance at the new edge midpoints exceeds `tol`, when
// a neighbouring face would flip, or when the vertex sits on an open boundary. Stops at the triangle budget or when no
// collapse passes. Deterministic: ties break on vertex ids.
import { project } from "./sdf.mjs";

class Heap {
  constructor() { this.a = []; }
  push(x) { const a = this.a; a.push(x); let i = a.length - 1; while (i > 0) { const p = (i - 1) >> 1; if (this.lt(a[p], a[i])) break; [a[p], a[i]] = [a[i], a[p]]; i = p; } }
  lt(x, y) { return x.c < y.c || (x.c === y.c && (x.u < y.u || (x.u === y.u && x.v <= y.v))); }
  pop() { const a = this.a, top = a[0], last = a.pop(); if (a.length) { a[0] = last; let i = 0; for (;;) { let m = i; const l = 2 * i + 1, r = l + 1; if (l < a.length && !this.lt(a[m], a[l])) m = l; if (r < a.length && !this.lt(a[m], a[r])) m = r; if (m === i) break; [a[m], a[i]] = [a[i], a[m]]; i = m; } } return top; }
}
const qadd = (q, p) => { for (let i = 0; i < 10; i++) q[i] += p[i]; };
const qerr = (q, x, y, z) => q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9];

export function decimate(F, M, { target, tol, minDot = 0.1 }) {
  const P = Float64Array.from(M.pos), V = P.length / 3, T = M.tri, nf = T.length / 3, alive = new Uint8Array(nf).fill(1), vf = Array.from({ length: V }, () => new Set()), Q = Array.from({ length: V }, () => new Float64Array(10)), ver = new Uint32Array(V), gone = new Uint8Array(V);
  const nrm = (a, b, c) => { const u = [P[b * 3] - P[a * 3], P[b * 3 + 1] - P[a * 3 + 1], P[b * 3 + 2] - P[a * 3 + 2]], w = [P[c * 3] - P[a * 3], P[c * 3 + 1] - P[a * 3 + 1], P[c * 3 + 2] - P[a * 3 + 2]]; return [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]; };
  for (let f = 0; f < nf; f++) {
    const [a, b, c] = [T[f * 3], T[f * 3 + 1], T[f * 3 + 2]]; for (const v of [a, b, c]) vf[v].add(f);
    const n = nrm(a, b, c), A = Math.hypot(...n); if (A < 1e-18) continue; const u = n.map((v) => v / A), d = -(u[0] * P[a * 3] + u[1] * P[a * 3 + 1] + u[2] * P[a * 3 + 2]), w = A / 2;
    const p = [u[0] * u[0], u[0] * u[1], u[0] * u[2], u[0] * d, u[1] * u[1], u[1] * u[2], u[1] * d, u[2] * u[2], u[2] * d, d * d].map((v) => v * w);
    for (const v of [a, b, c]) qadd(Q[v], p);
  }
  const key = (a, b) => (a < b ? a * 4294967296 + b : b * 4294967296 + a), uses = new Map();
  for (let f = 0; f < nf; f++) for (let e = 0; e < 3; e++) { const k = key(T[f * 3 + e], T[f * 3 + (e + 1) % 3]); uses.set(k, (uses.get(k) || 0) + 1); }
  const locked = new Uint8Array(V); for (const [k, n] of uses) if (n === 1) { locked[Math.floor(k / 4294967296)] = 1; locked[k % 4294967296] = 1; }
  const nbrs = (v) => { const s = new Set(); for (const f of vf[v]) for (let e = 0; e < 3; e++) { const w = T[f * 3 + e]; if (w !== v) s.add(w); } return s; };
  const heap = new Heap(), cand = (a, b) => { const q = new Float64Array(10); qadd(q, Q[a]); qadd(q, Q[b]); const m = [(P[a * 3] + P[b * 3]) / 2, (P[a * 3 + 1] + P[b * 3 + 1]) / 2, (P[a * 3 + 2] + P[b * 3 + 2]) / 2]; heap.push({ c: qerr(q, ...m) + 1e-9 * Math.hypot(P[a * 3] - P[b * 3], P[a * 3 + 1] - P[b * 3 + 1], P[a * 3 + 2] - P[b * 3 + 2]), u: Math.min(a, b), v: Math.max(a, b), a, b, va: ver[a], vb: ver[b] }); };
  for (const k of uses.keys()) cand(Math.floor(k / 4294967296), k % 4294967296);
  let tris = nf, tried = 0, rejected = 0;
  while (tris > target && heap.a.length) {
    const e = heap.pop(); if (gone[e.a] || gone[e.b] || ver[e.a] !== e.va || ver[e.b] !== e.vb) continue;
    const { a, b } = e; if (locked[a] || locked[b]) continue; tried++;
    const m = project(F, [(P[a * 3] + P[b * 3]) / 2, (P[a * 3 + 1] + P[b * 3 + 1]) / 2, (P[a * 3 + 2] + P[b * 3 + 2]) / 2], 5, tol), around = new Set([...vf[a], ...vf[b]]), shared = [...vf[a]].filter((f) => vf[b].has(f));
    let ok = m[3] <= tol * 0.25;
    const oldP = [P[a * 3], P[a * 3 + 1], P[a * 3 + 2]];
    const sag = (f, rm) => {   // field distance at the centroid and the three edge midpoints of face f, with vertex b read as a
      const v = [0, 1, 2].map((k) => (rm && T[f * 3 + k] === b ? a : T[f * 3 + k])), at = (u, w, s) => Math.abs(F.d((P[u * 3] + P[w * 3] + (s ? P[s * 3] : 0)) / (s ? 3 : 2), (P[u * 3 + 1] + P[w * 3 + 1] + (s ? P[s * 3 + 1] : 0)) / (s ? 3 : 2), (P[u * 3 + 2] + P[w * 3 + 2] + (s ? P[s * 3 + 2] : 0)) / (s ? 3 : 2)));
      return Math.max(at(v[0], v[1], v[2]), at(v[0], v[1]), at(v[1], v[2]), at(v[2], v[0]));
    };
    const oldSag = new Map(); if (ok) for (const f of around) if (!shared.includes(f)) oldSag.set(f, sag(f, false));
    if (ok) {
      P[a * 3] = m[0]; P[a * 3 + 1] = m[1]; P[a * 3 + 2] = m[2];
      for (const f of around) {
        if (shared.includes(f)) continue; const t = [T[f * 3], T[f * 3 + 1], T[f * 3 + 2]].map((v) => (v === b ? a : v));
        const bo = [T[f * 3], T[f * 3 + 1], T[f * 3 + 2]], pb = P[b * 3], n1 = nrm(...t), A1 = Math.hypot(...n1);
        // old normal: use the old position of a for faces on a, and b for faces on b
        const keepA = vf[a].has(f); if (keepA) { P[a * 3] = oldP[0]; P[a * 3 + 1] = oldP[1]; P[a * 3 + 2] = oldP[2]; }
        const n0 = nrm(...bo), A0 = Math.hypot(...n0); if (keepA) { P[a * 3] = m[0]; P[a * 3 + 1] = m[1]; P[a * 3 + 2] = m[2]; }
        if (A1 < 1e-14 || (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2]) / (A0 * A1 || 1) < minDot) { ok = false; break; }
        void pb;
      }
    }
    if (ok) for (const [f, was] of oldSag) { const now = sag(f, true); if (now > tol && now > was + 1e-4) { ok = false; break; } }   // a face may not sag past tol unless it already did, and then not further
    if (!ok) { P[a * 3] = oldP[0]; P[a * 3 + 1] = oldP[1]; P[a * 3 + 2] = oldP[2]; rejected++; continue; }
    for (const f of shared) { alive[f] = 0; tris--; for (let k = 0; k < 3; k++) vf[T[f * 3 + k]].delete(f); }
    for (const f of vf[b]) { for (let k = 0; k < 3; k++) if (T[f * 3 + k] === b) T[f * 3 + k] = a; vf[a].add(f); }
    vf[b].clear(); gone[b] = 1; qadd(Q[a], Q[b]); ver[a]++;
    for (const w of nbrs(a)) { ver[w]++; }
    for (const w of nbrs(a)) cand(a, w);
  }
  const map = new Int32Array(V).fill(-1), pos = [], tri = [];
  for (let f = 0; f < nf; f++) if (alive[f]) for (let k = 0; k < 3; k++) { const v = T[f * 3 + k]; if (map[v] < 0) { map[v] = pos.length / 3; pos.push(P[v * 3], P[v * 3 + 1], P[v * 3 + 2]); } tri.push(map[v]); }
  const Pf = Float32Array.from(pos), N = new Float32Array(Pf.length);
  for (let v = 0; v < Pf.length; v += 3) { const g = F.grad(Pf[v], Pf[v + 1], Pf[v + 2]); N[v] = g[0]; N[v + 1] = g[1]; N[v + 2] = g[2]; }
  return { pos: Pf, nrm: N, tri: Uint32Array.from(tri), stats: { from: nf, to: tris, tried, rejected } };
}

// field distance at triangle centroids and edge midpoints, metres: the same honest sag measure as meshError, for triangles
export function triError(F, M) {
  const p = M.pos, d = [], t = M.tri, at = (a, b, c) => d.push(Math.abs(F.d((p[a * 3] + p[b * 3] + p[c * 3]) / 3, (p[a * 3 + 1] + p[b * 3 + 1] + p[c * 3 + 1]) / 3, (p[a * 3 + 2] + p[b * 3 + 2] + p[c * 3 + 2]) / 3)));
  for (let i = 0; i < t.length; i += 3) { at(t[i], t[i + 1], t[i + 2]); for (let e = 0; e < 3; e++) { const a = t[i + e], b = t[i + (e + 1) % 3]; d.push(Math.abs(F.d((p[a * 3] + p[b * 3]) / 2, (p[a * 3 + 1] + p[b * 3 + 1]) / 2, (p[a * 3 + 2] + p[b * 3 + 2]) / 2))); } }
  d.sort((x, y) => x - y); return { n: d.length, p50: d[d.length >> 1], p99: d[Math.floor(0.99 * (d.length - 1))], max: d[d.length - 1] };
}
