// Baking from the sculpt field onto a UV-mapped game mesh. The reference is the field itself, so there is
// no high-poly mesh to ray cast: each texel finds its sculpt surface point by Newton steps from the game
// surface, then reads normal, curvature and cavity exactly, and ambient occlusion and thickness from a
// coarse distance volume. Normal maps are tangent space, X = +u, Y = +v (v down the image, DirectX style).
import { project } from "./sdf.mjs";

export class Volume {
  constructor(F, lo, hi, h = 0.0025) {
    this.lo = lo; this.h = h; this.n = [0, 1, 2].map((i) => Math.ceil((hi[i] - lo[i]) / h) + 2); const [nx, ny, nz] = this.n;
    this.g = new Float32Array(nx * ny * nz);
    for (let k = 0; k < nz; k++) for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) this.g[(k * ny + j) * nx + i] = Math.max(-0.1, Math.min(0.1, F.d(lo[0] + i * h, lo[1] + j * h, lo[2] + k * h)));
  }
  at(x, y, z) {
    const [nx, ny, nz] = this.n, fx = (x - this.lo[0]) / this.h, fy = (y - this.lo[1]) / this.h, fz = (z - this.lo[2]) / this.h;
    if (fx < 0 || fy < 0 || fz < 0 || fx >= nx - 1 || fy >= ny - 1 || fz >= nz - 1) return 0.1;
    const i = Math.floor(fx), j = Math.floor(fy), k = Math.floor(fz), tx = fx - i, ty = fy - j, tz = fz - k, g = this.g, b = (k * ny + j) * nx + i, s = nx, t = nx * ny;
    const c00 = g[b] * (1 - tx) + g[b + 1] * tx, c10 = g[b + s] * (1 - tx) + g[b + s + 1] * tx, c01 = g[b + t] * (1 - tx) + g[b + t + 1] * tx, c11 = g[b + s + t] * (1 - tx) + g[b + s + t + 1] * tx;
    return (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
  }
}

const DIRS = (() => { const out = [], n = 24; for (let i = 0; i < n; i++) { const z = Math.sqrt((i + 0.5) / n), r = Math.sqrt(1 - z * z), a = i * 2.399963229728653; out.push([r * Math.cos(a), r * Math.sin(a), z]); } return out; })();
const hash = (n) => { n = (n ^ 61) ^ (n >>> 16); n = Math.imul(n + (n << 3), 0x27d4eb2d); n ^= n >>> 15; return (n >>> 0) / 4294967296; };
const frameOf = (n) => { const a = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0], t = [a[1] * n[2] - a[2] * n[1], a[2] * n[0] - a[0] * n[2], a[0] * n[1] - a[1] * n[0]], l = Math.hypot(...t); const T = t.map((v) => v / l); return [T, [n[1] * T[2] - n[2] * T[1], n[2] * T[0] - n[0] * T[2], n[0] * T[1] - n[1] * T[0]]]; };

// the sampler: everything the painter and the maps need at one surface point
export function makeSampler(F, Fskin, vol, { aoRay = 0.05 } = {}) {
  const hairF = Fskin || F;
  return function sample(p0, seed = 0, cage = 0.012) {
    const q = project(F, p0, 5, cage), P = [q[0], q[1], q[2]], N = F.grad(P[0], P[1], P[2]);
    const moved = Math.hypot(P[0] - p0[0], P[1] - p0[1], P[2] - p0[2]);
    const [T, B] = frameOf(N), rot = hash(seed * 7 + 1) * 6.2831853, cr = Math.cos(rot), sr = Math.sin(rot);
    let occ = 0;
    for (const d of DIRS) {
      const x = d[0] * cr - d[1] * sr, y = d[0] * sr + d[1] * cr, z = d[2], dir = [T[0] * x + B[0] * y + N[0] * z, T[1] * x + B[1] * y + N[1] * z, T[2] * x + B[2] * y + N[2] * z];
      let t = 0.003;
      for (let s = 0; s < 14 && t < aoRay; s++) { const dd = vol.at(P[0] + N[0] * 0.002 + dir[0] * t, P[1] + N[1] * 0.002 + dir[1] * t, P[2] + N[2] * 0.002 + dir[2] * t); if (dd < 0.0007) { occ += 1 - t / aoRay; break; } t += Math.max(dd, 0.0015); }
    }
    const ao = 1 - occ / DIRS.length;
    let thick = 0.08; for (let t = 0.0015; t < 0.08; t += 0.001) if (vol.at(P[0] - N[0] * t, P[1] - N[1] * t, P[2] - N[2] * t) > 0.0004) { thick = t; break; }
    const h = 0.0025, lap = (F.d(P[0] + h, P[1], P[2]) + F.d(P[0] - h, P[1], P[2]) + F.d(P[0], P[1] + h, P[2]) + F.d(P[0], P[1] - h, P[2]) + F.d(P[0], P[1], P[2] + h) + F.d(P[0], P[1], P[2] - h)) / (h * h), kappa = lap / 2;
    let cav = 0, ws = 0; [0.003, 0.006, 0.012].forEach((r, k) => { const w = 1 / (k + 1), dd = F.d(P[0] + N[0] * r, P[1] + N[1] * r, P[2] + N[2] * r); cav += w * Math.max(0, Math.min(1, (r - dd) / r)); ws += w; });
    const hair = hairF === F ? 0 : (hairF.d(P[0], P[1], P[2]) > 0.0006 ? 1 : 0);
    return { P, N, ao, thick, kappa, cav: cav / ws, hair, moved, resid: Math.abs(F.d(P[0], P[1], P[2])) };
  };
}

export const curvByte = (kappa) => Math.max(0, Math.min(255, Math.round(128 + 127 * Math.tanh(kappa / 60))));   // the Low Water convention
export const cavByte = (cav) => Math.max(0, Math.min(255, Math.round(255 * cav)));

// rasterise the UV triangles; calls fn(texelIndex, baryPos, baryNrm, tangent, bitangent)
export function rasterUv(U, res, fn) {
  const cover = new Uint8Array(res * res);
  for (let q = 0; q < U.quads.length; q += 4) for (const [a, b, c] of [[0, 1, 2], [0, 2, 3]]) {
    const v = [U.quads[q + a], U.quads[q + b], U.quads[q + c]], tx = v.map((i) => U.uv[i * 2] * res), ty = v.map((i) => U.uv[i * 2 + 1] * res);
    const den = (ty[1] - ty[2]) * (tx[0] - tx[2]) + (tx[2] - tx[1]) * (ty[0] - ty[2]); if (Math.abs(den) < 1e-9) continue;
    const x0 = Math.max(0, Math.floor(Math.min(...tx))), x1 = Math.min(res - 1, Math.ceil(Math.max(...tx))), y0 = Math.max(0, Math.floor(Math.min(...ty))), y1 = Math.min(res - 1, Math.ceil(Math.max(...ty)));
    const P = (i, k) => U.pos[v[i] * 3 + k], du = [0, 1, 2].map((k) => ((P(1, k) - P(0, k)) * (ty[2] - ty[0]) - (P(2, k) - P(0, k)) * (ty[1] - ty[0])) / ((tx[1] - tx[0]) * (ty[2] - ty[0]) - (tx[2] - tx[0]) * (ty[1] - ty[0]) || 1e-12)), dv = [0, 1, 2].map((k) => ((P(2, k) - P(0, k)) * (tx[1] - tx[0]) - (P(1, k) - P(0, k)) * (tx[2] - tx[0])) / ((tx[1] - tx[0]) * (ty[2] - ty[0]) - (tx[2] - tx[0]) * (ty[1] - ty[0]) || 1e-12));
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
      const px = x + 0.5, py = y + 0.5, l0 = ((ty[1] - ty[2]) * (px - tx[2]) + (tx[2] - tx[1]) * (py - ty[2])) / den, l1 = ((ty[2] - ty[0]) * (px - tx[2]) + (tx[0] - tx[2]) * (py - ty[2])) / den, l2 = 1 - l0 - l1;
      if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6) continue;
      const pos = [0, 1, 2].map((k) => l0 * P(0, k) + l1 * P(1, k) + l2 * P(2, k)), nr = [0, 1, 2].map((k) => l0 * U.nrm[v[0] * 3 + k] + l1 * U.nrm[v[1] * 3 + k] + l2 * U.nrm[v[2] * 3 + k]), nl = Math.hypot(...nr);
      cover[y * res + x] = 1; fn(y * res + x, pos, nr.map((s) => s / nl), du, dv);
    }
  }
  return cover;
}

export function bakeMaps(U, sample, res) {
  const nT = res * res, normal = new Uint8Array(nT * 3), ao = new Uint8Array(nT), curv = new Uint8Array(nT), thick = new Uint8Array(nT), pos = new Float32Array(nT * 3), nrmW = new Float32Array(nT * 3), meta = new Uint8Array(nT);
  const stats = { texels: 0, cageViolations: 0, residMax: 0, movedMax: 0, normalErrDegMax: 0, normalErrDegP99: 0 }, errs = [];
  const cover = rasterUv(U, res, (t, p0, n0, du, dv) => {
    const S = sample(p0, t); stats.texels++; if (S.moved > 0.0099) stats.cageViolations++; stats.residMax = Math.max(stats.residMax, S.resid); stats.movedMax = Math.max(stats.movedMax, S.moved);
    let T = du.map((v, i) => v - n0[i] * (du[0] * n0[0] + du[1] * n0[1] + du[2] * n0[2])); let tl = Math.hypot(...T) || 1; T = T.map((v) => v / tl);
    const Bv = [n0[1] * T[2] - n0[2] * T[1], n0[2] * T[0] - n0[0] * T[2], n0[0] * T[1] - n0[1] * T[0]], sgn = Bv[0] * dv[0] + Bv[1] * dv[1] + Bv[2] * dv[2] < 0 ? -1 : 1, B = Bv.map((v) => v * sgn);
    const nt = [S.N[0] * T[0] + S.N[1] * T[1] + S.N[2] * T[2], S.N[0] * B[0] + S.N[1] * B[1] + S.N[2] * B[2], S.N[0] * n0[0] + S.N[1] * n0[1] + S.N[2] * n0[2]];
    const q8 = nt.map((v) => Math.max(0, Math.min(255, Math.round((v * 0.5 + 0.5) * 255)))), dq = q8.map((v) => v / 255 * 2 - 1), dl = Math.hypot(...dq) || 1;
    errs.push(Math.acos(Math.max(-1, Math.min(1, (dq[0] * nt[0] + dq[1] * nt[1] + dq[2] * nt[2]) / dl))) * 180 / Math.PI);
    normal.set(q8, t * 3); ao[t] = Math.round(S.ao * 255); curv[t] = curvByte(S.kappa); thick[t] = Math.round(Math.min(1, S.thick / 0.04) * 255);
    pos.set(S.P, t * 3); nrmW.set(S.N, t * 3); meta[t] = (S.hair ? 1 : 0) | (Math.round(S.cav * 127) << 1);
  });
  errs.sort((a, b) => a - b); stats.normalErrDegMax = errs[errs.length - 1] || 0; stats.normalErrDegP99 = errs[Math.floor(errs.length * 0.99)] || 0; stats.coverage = stats.texels / nT;
  return { normal, ao, curv, thick, pos, nrmW, meta, cover, stats, res };
}

// grow covered texels outward so bilinear filtering across seams never reads background
export function dilate(img, ch, cover, res, passes = 4) {
  const cov = Uint8Array.from(cover);
  for (let p = 0; p < passes; p++) {
    const add = [];
    for (let y = 0; y < res; y++) for (let x = 0; x < res; x++) { if (cov[y * res + x]) continue; for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) { const xx = x + dx, yy = y + dy; if (xx >= 0 && yy >= 0 && xx < res && yy < res && cov[yy * res + xx]) { add.push([y * res + x, yy * res + xx]); dy = 2; break; } } }
    for (const [t, s] of add) { for (let c = 0; c < ch; c++) img[t * ch + c] = img[s * ch + c]; cov[t] = 1; }
  }
  return cov;
}
