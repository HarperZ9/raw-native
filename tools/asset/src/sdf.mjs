// Signed-distance field from an ordered list of brush strokes. Pure ESM, no dependencies.
// A stroke is { id, op: add|sub|and, shape: ell|cap|box|tor, k, mirror?, tag?, ...shape params }, in metres.
// The field is a left fold: d = op(d, shape_d, k) in file order, the way a sculptor's strokes land.
// Distances are approximate (ellipsoids use the first-order estimate); every bound in DESIGN.md is
// measured against this field, so the field is the truth and meshes are judged against it.
const hypot3 = (x, y, z) => Math.sqrt(x * x + y * y + z * z);
export const smin = (a, b, k) => { if (k <= 0) return Math.min(a, b); const h = Math.max(k - Math.abs(a - b), 0) / k; return Math.min(a, b) - h * h * k * 0.25; };
export const smax = (a, b, k) => -smin(-a, -b, k);

// rot is [pitch, yaw, roll] degrees; returns the 3x3 that maps a world offset into the shape's frame
function rotInv(rot) {
  if (!rot) return null;
  const [px, py, pz] = rot.map((v) => v * Math.PI / 180), cx = Math.cos(px), sx = Math.sin(px), cy = Math.cos(py), sy = Math.sin(py), cz = Math.cos(pz), sz = Math.sin(pz);
  const Rx = [1, 0, 0, 0, cx, -sx, 0, sx, cx], Ry = [cy, 0, sy, 0, 1, 0, -sy, 0, cy], Rz = [cz, -sz, 0, sz, cz, 0, 0, 0, 1];
  const mul = (A, B) => { const C = new Array(9).fill(0); for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) for (let k = 0; k < 3; k++) C[i * 3 + j] += A[i * 3 + k] * B[k * 3 + j]; return C; };
  const R = mul(Ry, mul(Rx, Rz));
  return [R[0], R[3], R[6], R[1], R[4], R[7], R[2], R[5], R[8]];   // R transpose: world offset to shape frame
}
const frame = (m, x, y, z) => m ? [m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z, m[6] * x + m[7] * y + m[8] * z] : [x, y, z];

import { PRIMS } from "./prims.mjs";
import { applyMods, modBox, sminV, sminW } from "./mods.mjs";
const H = 1e-5;
// legacy shapes have no analytic normal: central differences of the leaf alone (6 evaluations), only when asked
function legacyLeaf(s) {
  const f = legacyShape(s);
  return (x, y, z, g) => {
    if (g) { const a = f(x + H, y, z) - f(x - H, y, z), b = f(x, y + H, z) - f(x, y - H, z), c = f(x, y, z + H) - f(x, y, z - H), l = hypot3(a, b, c) || 1; g[0] = a / l; g[1] = b / l; g[2] = c / l; }
    return f(x, y, z);
  };
}
export const KITS = new Set(Object.keys(PRIMS));
export function shapeFn(s) { return applyMods(PRIMS[s.shape] ? PRIMS[s.shape](s) : legacyLeaf(s), s.mods); }
function legacyShape(s) {
  const m = rotInv(s.rot);
  if (s.shape === "ell") {
    const [cx, cy, cz] = s.c, [rx, ry, rz] = s.r;
    return (x, y, z) => { const p = frame(m, x - cx, y - cy, z - cz), k0 = hypot3(p[0] / rx, p[1] / ry, p[2] / rz), k1 = hypot3(p[0] / (rx * rx), p[1] / (ry * ry), p[2] / (rz * rz)); return k1 > 1e-12 ? k0 * (k0 - 1) / k1 : -Math.min(rx, ry, rz); };
  }
  if (s.shape === "cap") {   // round cone from a to b, radius r0 to r1
    const [ax, ay, az] = s.a, bx = s.b[0] - ax, by = s.b[1] - ay, bz = s.b[2] - az, bb = bx * bx + by * by + bz * bz || 1e-12, [r0, r1] = Array.isArray(s.r) ? s.r : [s.r, s.r];
    return (x, y, z) => { const px = x - ax, py = y - ay, pz = z - az, h = Math.min(1, Math.max(0, (px * bx + py * by + pz * bz) / bb)); return hypot3(px - bx * h, py - by * h, pz - bz * h) - (r0 + (r1 - r0) * h); };
  }
  if (s.shape === "box") {
    const [cx, cy, cz] = s.c, [hx, hy, hz] = s.r, rd = s.round || 0;
    return (x, y, z) => { const p = frame(m, x - cx, y - cy, z - cz), qx = Math.abs(p[0]) - hx + rd, qy = Math.abs(p[1]) - hy + rd, qz = Math.abs(p[2]) - hz + rd; return hypot3(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0)) + Math.min(Math.max(qx, qy, qz), 0) - rd; };
  }
  if (s.shape === "tor") {
    const [cx, cy, cz] = s.c;
    return (x, y, z) => { const p = frame(m, x - cx, y - cy, z - cz); return Math.hypot(Math.hypot(p[0], p[2]) - s.R, p[1]) - s.r; };
  }
  throw new Error(`sdf: unknown shape "${s.shape}" in stroke ${s.id}`);
}

// the box a stroke can reach, for culling (rotated shapes use the largest radius)
function kitBox(s) {
  const around = (c, r) => ({ lo: c.map((v) => v - r), hi: c.map((v) => v + r) });
  if (s.shape === "rbox") return around(s.c, s.rot ? hypot3(...s.r) : Math.max(...s.r));
  if (s.shape === "ellb") return around(s.c, Math.max(...s.r));
  if (s.shape === "torx") return around(s.c, s.R + s.r);
  if (s.shape === "ext") { const P = s.prof, pe = P.kind === "circle" ? P.r : P.kind === "rrect" ? Math.hypot(P.hx, P.hy) : Math.max(...P.pts.map((p) => Math.hypot(...p))); return around(s.c, Math.hypot(pe, s.h)); }
  const rm = Math.max(...(Array.isArray(s.r) ? s.r : [s.r]));   // rcone, cone
  return { lo: s.a.map((v, i) => Math.min(v, s.b[i]) - rm), hi: s.a.map((v, i) => Math.max(v, s.b[i]) + rm) };
}
export const strokeBox = (s) => modBox(KITS.has(s.shape) ? kitBox(s) : baseBox(s), s.mods);
function baseBox(s) {
  const around = (c, r) => ({ lo: c.map((v, i) => v - (Array.isArray(r) ? r[i] : r)), hi: c.map((v, i) => v + (Array.isArray(r) ? r[i] : r)) });
  if (s.shape === "ell") return around(s.c, s.rot ? Math.max(...s.r) : s.r);
  if (s.shape === "box") return around(s.c, s.rot ? hypot3(...s.r) : s.r.map((v) => v));
  if (s.shape === "cap") { const rm = Array.isArray(s.r) ? Math.max(...s.r) : s.r; return { lo: s.a.map((v, i) => Math.min(v, s.b[i]) - rm), hi: s.a.map((v, i) => Math.max(v, s.b[i]) + rm) }; }
  return around(s.c, s.R + s.r);
}

export const mirrorStroke = (s) => {
  const f = (p) => p && [-p[0], p[1], p[2]], o = { ...s, id: s.id + ".m", mirror: false };
  if (s.mods?.some((m) => m.c || m.mask || m.kind === "mirror" || m.kind === "repeat" || m.src?.dir)) o.mods = s.mods.map((m) => ({ ...m, c: m.c && f(m.c), mask: m.mask && { ...m.mask, c: f(m.mask.c) }, src: m.src?.dir ? { ...m.src, dir: f(m.src.dir) } : m.src }));
  for (const key of ["c", "a", "b"]) if (s[key]) o[key] = f(s[key]);
  if (s.rot) o.rot = [s.rot[0], -s.rot[1], -s.rot[2]];   // yaw and roll flip across the x = 0 plane
  return o;
};
export const expandStrokes = (strokes) => strokes.flatMap((s) => (s.mirror ? [s, mirrorStroke(s)] : [s]));

const foldV = (cur, v, s) => (s.op === "add" ? sminV(cur, v, s.k, s.cubic) : s.op === "sub" ? -sminV(-cur, v, s.k, s.cubic) : -sminV(-cur, -v, s.k, s.cubic));
function foldG(cur, gc, v, gv, s) {   // sub: -smin(-d, v); and: -smin(-d, -v); gradient weights are the exact derivative
  const a = s.op === "add" ? cur : -cur, b = s.op === "and" ? -v : v, w = sminW(a, b, s.k, s.cubic), sb = s.op === "sub" ? -1 : 1;
  for (let i = 0; i < 3; i++) gc[i] = w * gc[i] + (1 - w) * sb * gv[i];
  return foldV(cur, v, s);
}
const CULL_MARGIN = 0.02;   // how far outside its box a stroke can still change the field inside the narrow band
export class Field {
  constructor(strokes, { exclude = null, cull = true } = {}) {
    this.cull = cull;
    this.list = expandStrokes(strokes).filter((s) => !(exclude && s.tag && exclude.has(s.tag))).map((s) => {
      const k = s.k ?? 0.01, b = strokeBox(s), pad = k + CULL_MARGIN;
      return { f: shapeFn(s), op: s.op, cubic: s.blend === "cubic", hair: s.tag === "hair", k, lo: b.lo.map((v) => v - pad), hi: b.hi.map((v) => v + pad), tlo: b.lo, thi: b.hi };
    });
  }
  d(x, y, z) {
    let d = 1e3, h = 1e3, hairy = false;
    for (const s of this.list) {
      if (this.cull && s.op !== "and" && (x < s.lo[0] || x > s.hi[0] || y < s.lo[1] || y > s.hi[1] || z < s.lo[2] || z > s.hi[2])) continue;
      const v = s.f(x, y, z);
      if (s.hair) { hairy = true; h = foldV(h, v, s); } else d = foldV(d, v, s);
    }
    return hairy ? smin(d, h, 0.003) : d;   // strokes tagged "hair" fold on their own, so a hair clip never cuts skin
  }
  // value and analytic gradient of the whole fold (exact derivative of the smooth blends); returns d, writes unnormalised g
  dg(x, y, z, g) {
    let d = 1e3, h = 1e3, hairy = false; const gd = [0, 0, 0], gh = [0, 0, 0], gv = [0, 0, 0];
    for (const s of this.list) {
      if (this.cull && s.op !== "and" && (x < s.lo[0] || x > s.hi[0] || y < s.lo[1] || y > s.hi[1] || z < s.lo[2] || z > s.hi[2])) continue;
      const v = s.f(x, y, z, gv);
      if (s.hair) { hairy = true; h = foldG(h, gh, v, gv, s); } else d = foldG(d, gd, v, gv, s);
    }
    if (hairy) { const w = sminW(d, h, 0.003, false); for (let i = 0; i < 3; i++) g[i] = w * gd[i] + (1 - w) * gh[i]; return sminV(d, h, 0.003, false); }
    g[0] = gd[0]; g[1] = gd[1]; g[2] = gd[2]; return d;
  }
  gradFD(x, y, z, h = 0.0004) {
    const g = [this.d(x + h, y, z) - this.d(x - h, y, z), this.d(x, y + h, z) - this.d(x, y - h, z), this.d(x, y, z + h) - this.d(x, y, z - h)], l = hypot3(...g) || 1;
    return [g[0] / l, g[1] / l, g[2] / l];
  }
  grad(x, y, z) { const g = [0, 0, 0]; this.dg(x, y, z, g); const l = hypot3(...g) || 1; return [g[0] / l, g[1] / l, g[2] / l]; }
  // true when some add stroke could place surface inside the box (brick test for the sparse grid)
  touches(lo, hi, band) {
    for (const s of this.list) if (s.op === "add" && !(hi[0] < s.tlo[0] - band - s.k || lo[0] > s.thi[0] + band + s.k || hi[1] < s.tlo[1] - band - s.k || lo[1] > s.thi[1] + band + s.k || hi[2] < s.tlo[2] - band - s.k || lo[2] > s.thi[2] + band + s.k)) return true;
    return false;
  }
  bounds(pad = 0.01) {
    const lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (const s of this.list) if (s.op === "add") for (let i = 0; i < 3; i++) { lo[i] = Math.min(lo[i], s.tlo[i] - pad); hi[i] = Math.max(hi[i], s.thi[i] + pad); }
    return { lo, hi };
  }
}

// Newton steps to the nearest surface point; returns [x, y, z, |residual|]
export function project(F, p, steps = 4, maxMove = 0.01) {
  let [x, y, z] = p;
  for (let i = 0; i < steps; i++) {
    const d = F.d(x, y, z), g = F.grad(x, y, z);
    let mx = -d * g[0], my = -d * g[1], mz = -d * g[2]; const m = hypot3(mx, my, mz);
    if (m > maxMove) { const s = maxMove / m; mx *= s; my *= s; mz *= s; }
    x += mx; y += my; z += mz;
    if (m < 1e-6) break;
  }
  return [x, y, z, Math.abs(F.d(x, y, z))];
}
