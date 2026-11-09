// Exact signed-distance primitives with analytic normals. A leaf is f(x, y, z, g) -> d; when g (length 3) is
// given the unit outward normal is written into it. Bounds per kind are in DESIGN.md section 10.
// Published formulas used, cited at the line: Inigo Quilez, "Distance functions", iquilezles.org/articles/distfunctions
// (rounded box, round cone, extrusion of a 2D distance, torus). Normals and the polygon and ellipsoid bounds are derived here.
const h3 = Math.hypot;

// rot = [pitch, yaw, roll] degrees; m maps a world offset into the shape frame (same convention as sdf.mjs)
export function rotM(rot) {
  if (!rot) return null;
  const [px, py, pz] = rot.map((v) => v * Math.PI / 180), cx = Math.cos(px), sx = Math.sin(px), cy = Math.cos(py), sy = Math.sin(py), cz = Math.cos(pz), sz = Math.sin(pz);
  const Rx = [1, 0, 0, 0, cx, -sx, 0, sx, cx], Ry = [cy, 0, sy, 0, 1, 0, -sy, 0, cy], Rz = [cz, -sz, 0, sz, cz, 0, 0, 0, 1];
  const mul = (A, B) => { const C = new Array(9).fill(0); for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) for (let k = 0; k < 3; k++) C[i * 3 + j] += A[i * 3 + k] * B[k * 3 + j]; return C; };
  const R = mul(Ry, mul(Rx, Rz));
  return [R[0], R[3], R[6], R[1], R[4], R[7], R[2], R[5], R[8]];
}
const toLocal = (m, x, y, z) => m ? [m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z, m[6] * x + m[7] * y + m[8] * z] : [x, y, z];
const putWorld = (m, g, a, b, c) => { if (m) { g[0] = m[0] * a + m[3] * b + m[6] * c; g[1] = m[1] * a + m[4] * b + m[7] * c; g[2] = m[2] * a + m[5] * b + m[8] * c; } else { g[0] = a; g[1] = b; g[2] = c; } };
const sgn = (v) => (v < 0 ? -1 : 1);

// IQ sdRoundBox with half extents h and corner radius rd (the box is inset by rd and then rounded)
export function rbox(s) {
  const [cx, cy, cz] = s.c, [hx, hy, hz] = s.r, rd = s.round || 0, m = rotM(s.rot);
  return (x, y, z, g) => {
    const p = toLocal(m, x - cx, y - cy, z - cz), q0 = Math.abs(p[0]) - hx + rd, q1 = Math.abs(p[1]) - hy + rd, q2 = Math.abs(p[2]) - hz + rd;
    const o0 = Math.max(q0, 0), o1 = Math.max(q1, 0), o2 = Math.max(q2, 0), lo = h3(o0, o1, o2), mq = Math.max(q0, q1, q2);
    if (lo > 0) { if (g) putWorld(m, g, o0 / lo * sgn(p[0]), o1 / lo * sgn(p[1]), o2 / lo * sgn(p[2])); return lo - rd; }
    if (g) putWorld(m, g, mq === q0 ? sgn(p[0]) : 0, mq === q1 && mq !== q0 ? sgn(p[1]) : 0, mq === q2 && mq !== q0 && mq !== q1 ? sgn(p[2]) : 0);
    return mq - rd;
  };
}

// IQ sdRoundCone (arbitrary endpoints): sphere r0 at a, sphere r1 at b, joined by the tangent cone. r0 = r1 is a capsule.
// The three branches are the two sphere regions and the cone side; the normal is taken from the branch that won.
export function rcone(s) {
  const [ax, ay, az] = s.a, bx = s.b[0] - ax, by = s.b[1] - ay, bz = s.b[2] - az, [r0, r1] = Array.isArray(s.r) ? s.r : [s.r, s.r];
  const l2 = bx * bx + by * by + bz * bz, L = Math.sqrt(l2), rr = r0 - r1, a2 = l2 - rr * rr, il2 = 1 / l2, ux = bx / L, uy = by / L, uz = bz / L;
  if (!(a2 > 0)) throw new Error(`sdf: stroke ${s.id}: one end sphere swallows the other (|r0 - r1| >= length)`);
  return (x, y, z, g) => {
    const px = x - ax, py = y - ay, pz = z - az, yy = px * bx + py * by + pz * bz, zz = yy - l2;
    const cx = px * l2 - bx * yy, cy = py * l2 - by * yy, cz = pz * l2 - bz * yy, x2 = cx * cx + cy * cy + cz * cz, y2 = yy * yy * l2, z2 = zz * zz * l2, k = Math.sign(rr) * rr * rr * x2;
    if (Math.sign(zz) * a2 * z2 > k) { const d = Math.sqrt(x2 + z2) * il2; if (g) { const e = px - bx, f = py - by, q = pz - bz, n = h3(e, f, q) || 1; g[0] = e / n; g[1] = f / n; g[2] = q / n; } return d - r1; }
    if (Math.sign(yy) * a2 * y2 < k) { const d = Math.sqrt(x2 + y2) * il2; if (g) { const n = h3(px, py, pz) || 1; g[0] = px / n; g[1] = py / n; g[2] = pz / n; } return d - r0; }
    if (g) {
      const t = yy / l2, rx = px - t * bx, ry = py - t * by, rz = pz - t * bz, rl = h3(rx, ry, rz), ca = Math.sqrt(a2) / L, sa = rr / L;
      if (rl > 1e-12) { g[0] = ca * rx / rl + sa * ux; g[1] = ca * ry / rl + sa * uy; g[2] = ca * rz / rl + sa * uz; } else { g[0] = ux; g[1] = uy; g[2] = uz; }
    }
    return (Math.sqrt(x2 * a2 * il2) + yy * rr) * il2 - r0;
  };
}

// exact signed distance to a closed polygon (even-odd sign), with the gradient into out = [gx, gy]
export function poly2(pts, px, py, out) {
  let best = Infinity, bx = 0, by = 0, inside = false;
  for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
    const [x0, y0] = pts[j], [x1, y1] = pts[i], ex = x1 - x0, ey = y1 - y0, e2 = ex * ex + ey * ey, t = e2 > 0 ? Math.min(1, Math.max(0, ((px - x0) * ex + (py - y0) * ey) / e2)) : 0;
    const qx = x0 + ex * t, qy = y0 + ey * t, d2 = (px - qx) * (px - qx) + (py - qy) * (py - qy);
    if (d2 < best) { best = d2; bx = qx; by = qy; }
    if ((y1 > py) !== (y0 > py) && px < (x0 - x1) * (py - y1) / (y0 - y1) + x1) inside = !inside;
  }
  const d = Math.sqrt(best);
  if (out) { const s = (inside ? -1 : 1) / (d || 1); out[0] = (px - bx) * s; out[1] = (py - by) * s; }
  return inside ? -d : d;
}

// frustum with flat caps: the convex polygon (+-r0 at a, +-r1 at b) revolved about the axis; the meridian distance is exact
export function cone(s) {
  const [ax, ay, az] = s.a, bx = s.b[0] - ax, by = s.b[1] - ay, bz = s.b[2] - az, [r0, r1] = Array.isArray(s.r) ? s.r : [s.r, s.r], L = h3(bx, by, bz), ux = bx / L, uy = by / L, uz = bz / L;
  const pts = [[-r0, 0], [r0, 0], [r1, L], [-r1, L]], o = [0, 0];
  return (x, y, z, g) => {
    const px = x - ax, py = y - ay, pz = z - az, h = px * ux + py * uy + pz * uz, rx = px - h * ux, ry = py - h * uy, rz = pz - h * uz, rho = h3(rx, ry, rz);
    const d = poly2(pts, rho, h, g ? o : null);
    if (g) { const k = rho > 1e-12 ? o[0] / rho : 0; g[0] = k * rx + o[1] * ux; g[1] = k * ry + o[1] * uy; g[2] = k * rz + o[1] * uz; }
    return d;
  };
}

// IQ sdTorus: ring radius R, tube radius r, axis is local y
export function torx(s) {
  const [cx, cy, cz] = s.c, m = rotM(s.rot);
  return (x, y, z, g) => {
    const p = toLocal(m, x - cx, y - cy, z - cz), rho = Math.hypot(p[0], p[2]), q = rho - s.R, l = h3(q, p[1]);
    if (g) putWorld(m, g, rho > 1e-12 ? q / l * p[0] / rho : 0, p[1] / (l || 1), rho > 1e-12 ? q / l * p[2] / rho : 0);
    return l - s.r;
  };
}

// 2D profiles for extrusion: f(x, y, out) -> signed distance, gradient into out
function profile(P) {
  if (P.kind === "circle") return (x, y, o) => { const l = Math.hypot(x, y) || 1e-12; o[0] = x / l; o[1] = y / l; return l - P.r; };
  if (P.kind === "rrect") {
    const hx = P.hx, hy = P.hy, rd = P.round || 0;
    return (x, y, o) => {
      const q0 = Math.abs(x) - hx + rd, q1 = Math.abs(y) - hy + rd, o0 = Math.max(q0, 0), o1 = Math.max(q1, 0), l = Math.hypot(o0, o1);
      if (l > 0) { o[0] = o0 / l * sgn(x); o[1] = o1 / l * sgn(y); return l - rd; }
      if (q0 > q1) { o[0] = sgn(x); o[1] = 0; } else { o[0] = 0; o[1] = sgn(y); }
      return Math.max(q0, q1) - rd;
    };
  }
  if (P.kind === "poly") return (x, y, o) => poly2(P.pts, x, y, o);
  throw new Error(`sdf: unknown profile "${P.kind}"`);
}
// IQ opExtrusion: w = (d2(p.xy), |z| - h), d = min(max(w.x, w.y), 0) + length(max(w, 0)); profile in the local xy plane, extruded along z
export function ext(s) {
  const [cx, cy, cz] = s.c, m = rotM(s.rot), f2 = profile(s.prof), o = [0, 0], h = s.h;
  return (x, y, z, g) => {
    const p = toLocal(m, x - cx, y - cy, z - cz), w0 = f2(p[0], p[1], o), w1 = Math.abs(p[2]) - h, a = Math.max(w0, 0), b = Math.max(w1, 0), l = Math.hypot(a, b);
    if (l > 0) { if (g) putWorld(m, g, a / l * o[0], a / l * o[1], b / l * sgn(p[2])); return l; }
    if (g) { if (w0 > w1) putWorld(m, g, o[0], o[1], 0); else putWorld(m, g, 0, 0, sgn(p[2])); }
    return Math.max(w0, w1);
  };
}

// Ellipsoid. lo <= d_true <= hi at every point (signed). lower is the conservative member (smaller magnitude).
// k0 = |p / r| gives the lower bound (k0 - 1) min(r) (the scaling p / r is 1 / min(r) Lipschitz); the radial hit gives the upper.
export function ellipsoidInterval(r, px, py, pz) {
  const w0 = px / r[0], w1 = py / r[1], w2 = pz / r[2], k0 = h3(w0, w1, w2), k1 = h3(px / (r[0] * r[0]), py / (r[1] * r[1]), pz / (r[2] * r[2])), rmin = Math.min(...r), pl = h3(px, py, pz);
  if (k0 < 1e-9) return { lo: -Math.max(...r), hi: -rmin, est: -rmin, lower: -rmin, k0 };
  const est = k0 * (k0 - 1) / (k1 || 1e-12), lb = (k0 - 1) * rmin;
  if (k0 >= 1) { const ub = pl * (1 - 1 / k0); return { lo: lb, hi: ub, est: Math.min(ub, Math.max(lb, est)), lower: lb, k0 }; }
  const ub = pl * (1 / k0 - 1);
  return { lo: -ub, hi: lb, est: Math.min(lb, Math.max(-ub, est)), lower: lb, k0 };
}
export const ellipsoidLower = (r, px, py, pz) => (h3(px / r[0], py / r[1], pz / r[2]) - 1) * Math.min(...r);
export function ellb(s) {
  const [cx, cy, cz] = s.c, r = s.r, m = rotM(s.rot);
  return (x, y, z, g) => {
    const p = toLocal(m, x - cx, y - cy, z - cz), I = ellipsoidInterval(r, p[0], p[1], p[2]);
    if (g) { const a = p[0] / (r[0] * r[0]), b = p[1] / (r[1] * r[1]), c = p[2] / (r[2] * r[2]), l = h3(a, b, c); if (l > 1e-12) putWorld(m, g, a / l, b / l, c / l); else putWorld(m, g, 0, 1, 0); }
    return I.est;
  };
}
export const PRIMS = { rbox, rcone, cone, torx, ext, ellb };
