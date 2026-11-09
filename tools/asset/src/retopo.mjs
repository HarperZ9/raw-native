// Game-mesh retopology, version 1: a coarse quad net from the sculpt's own field, then tangential
// relaxation and re-projection. Output is 100 % quads with a regular valence-4 interior. It does not
// follow anatomical edge loops yet (guide curves are the next step in DESIGN.md); what it guarantees is
// the error bound against the sculpt and quad quality, both measured here.
import { extract } from "./extract.mjs";
import { project } from "./sdf.mjs";

function edgeUse(quads) {
  const use = new Map(), key = (a, b) => (a < b ? a * 4294967296 + b : b * 4294967296 + a);
  for (let q = 0; q < quads.length; q += 4) for (let e = 0; e < 4; e++) { const k = key(quads[q + e], quads[q + (e + 1) % 4]); use.set(k, (use.get(k) || 0) + 1); }
  return { use, key };
}
const mx = (a) => a.reduce((m, v) => (v > m ? v : m), -Infinity), mn = (a) => a.reduce((m, v) => (v < m ? v : m), Infinity);
const q = (a, f) => { const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(f * (s.length - 1)))]; };

export function quadQuality(M) {
  const p = M.pos, ang = [], asp = [];
  for (let i = 0; i < M.quads.length; i += 4) {
    const v = [0, 1, 2, 3].map((e) => [p[M.quads[i + e] * 3], p[M.quads[i + e] * 3 + 1], p[M.quads[i + e] * 3 + 2]]); let lmin = 1e9, lmax = 0;
    for (let e = 0; e < 4; e++) {
      const a = v[e], b = v[(e + 1) % 4], c = v[(e + 3) % 4], u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], w = [c[0] - a[0], c[1] - a[1], c[2] - a[2]], lu = Math.hypot(...u), lw = Math.hypot(...w);
      lmin = Math.min(lmin, lu); lmax = Math.max(lmax, lu); ang.push(Math.acos(Math.max(-1, Math.min(1, (u[0] * w[0] + u[1] * w[1] + u[2] * w[2]) / (lu * lw || 1)))) * 180 / Math.PI);
    }
    asp.push(lmax / (lmin || 1e-9));
  }
  return { quads: M.quads.length / 4, angleMin: mn(ang), angleMax: mx(ang), angleP05: q(ang, 0.05), angleP95: q(ang, 0.95), aspectP50: q(asp, 0.5), aspectP95: q(asp, 0.95), aspectMax: mx(asp) };
}

export function retopo(F, { lo, hi, cell, relax = 3, lambda = 0.5 }) {
  const M = extract(F, { lo, hi, cell }), V = M.pos.length / 3, { use, key } = edgeUse(M.quads), nb = Array.from({ length: V }, () => new Set()), fixed = new Uint8Array(V);
  for (let i = 0; i < M.quads.length; i += 4) for (let e = 0; e < 4; e++) { const a = M.quads[i + e], b = M.quads[i + (e + 1) % 4]; nb[a].add(b); nb[b].add(a); if (use.get(key(a, b)) === 1) fixed[a] = fixed[b] = 1; }
  let P = Float64Array.from(M.pos);
  for (let it = 0; it < relax; it++) {
    const next = Float64Array.from(P);
    for (let v = 0; v < V; v++) {
      if (fixed[v] || !nb[v].size) continue;
      let ax = 0, ay = 0, az = 0; for (const w of nb[v]) { ax += P[w * 3]; ay += P[w * 3 + 1]; az += P[w * 3 + 2]; }
      const n = nb[v].size, g = F.grad(P[v * 3], P[v * 3 + 1], P[v * 3 + 2]); let dx = ax / n - P[v * 3], dy = ay / n - P[v * 3 + 1], dz = az / n - P[v * 3 + 2];
      const dn = dx * g[0] + dy * g[1] + dz * g[2]; dx -= dn * g[0]; dy -= dn * g[1]; dz -= dn * g[2];
      const r = project(F, [P[v * 3] + dx * lambda, P[v * 3 + 1] + dy * lambda, P[v * 3 + 2] + dz * lambda], 3, cell);
      next[v * 3] = r[0]; next[v * 3 + 1] = r[1]; next[v * 3 + 2] = r[2];
    }
    P = next;
  }
  const pos = Float32Array.from(P), nrm = new Float32Array(pos.length);
  for (let v = 0; v < V; v++) { const g = F.grad(pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]); nrm[v * 3] = g[0]; nrm[v * 3 + 1] = g[1]; nrm[v * 3 + 2] = g[2]; }
  return { ...M, pos, nrm, boundary: fixed };
}

// distance from the sculpt at quad centres and mid-edges (where a net sags most), in metres
export function meshError(F, M) {
  const d = [], p = M.pos;
  for (let i = 0; i < M.quads.length; i += 4) {
    const v = [0, 1, 2, 3].map((e) => M.quads[i + e]);
    d.push(Math.abs(F.d((p[v[0] * 3] + p[v[1] * 3] + p[v[2] * 3] + p[v[3] * 3]) / 4, (p[v[0] * 3 + 1] + p[v[1] * 3 + 1] + p[v[2] * 3 + 1] + p[v[3] * 3 + 1]) / 4, (p[v[0] * 3 + 2] + p[v[1] * 3 + 2] + p[v[2] * 3 + 2] + p[v[3] * 3 + 2]) / 4)));
    for (let e = 0; e < 4; e++) { const a = v[e], b = v[(e + 1) % 4]; d.push(Math.abs(F.d((p[a * 3] + p[b * 3]) / 2, (p[a * 3 + 1] + p[b * 3 + 1]) / 2, (p[a * 3 + 2] + p[b * 3 + 2]) / 2))); }
  }
  return { n: d.length, p50: q(d, 0.5), p99: q(d, 0.99), max: mx(d) };
}
