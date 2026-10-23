// The tool's own portrait and review renderer: an orthographic software rasteriser with the Oil and Ink
// shading vocabulary (warm paraffin key, violet fill, light quantised into painted value steps, ink on
// silhouettes). It reads the asset's textures, so what the sheet shows is what the maps hold.
import { encodePng } from "./png.mjs";

const norm = (v) => { const l = Math.hypot(...v) || 1; return v.map((x) => x / l); };
const KEY = norm([-0.55, 0.62, 0.62]), FILL = norm([0.7, -0.1, 0.45]), RIM = norm([0.3, 0.35, -0.9]);

// view transform: yaw about y (degrees, positive turns the face toward the viewer's left), then elevation
export function viewMat(yaw, elev) {
  const a = yaw * Math.PI / 180, e = elev * Math.PI / 180, cy = Math.cos(a), sy = Math.sin(a), cx = Math.cos(e), sx = Math.sin(e);
  return (p) => { const x = cy * p[0] + sy * p[2], z0 = -sy * p[0] + cy * p[2]; return [x, cx * p[1] - sx * z0, sx * p[1] + cx * z0]; };
}

// parts: [{ pos, nrm, tri, uv?, albedo?: {w,h,rgb}, normalMap?, ao?, color?, spec?, thin? }]
export function renderView(parts, { yaw = 0, elev = 3, centre = [0, 0, 0], height = 0.3, W = 400, H = 480, ss = 2, bands = 5, ink = 1 } = {}) {
  const w = W * ss, h = H * ss, scale = h / height, V = viewMat(yaw, elev), col = new Float32Array(w * h * 3), depth = new Float32Array(w * h).fill(-1e9), id = new Int16Array(w * h).fill(-1);
  for (let i = 0; i < w * h; i++) { col[i * 3] = 0.47; col[i * 3 + 1] = 0.44; col[i * 3 + 2] = 0.41; }
  const tex = (m, u, v) => { const x = Math.max(0, Math.min(m.w - 1.001, u * m.w - 0.5)), y = Math.max(0, Math.min(m.h - 1.001, v * m.h - 0.5)), i = Math.floor(x), j = Math.floor(y), fx = x - i, fy = y - j, c = m.c, o = [], at = (a, b) => (b * m.w + a) * c; for (let k = 0; k < c; k++) o.push((m.data[at(i, j) + k] * (1 - fx) + m.data[at(i + 1, j) + k] * fx) * (1 - fy) + (m.data[at(i, j + 1) + k] * (1 - fx) + m.data[at(i + 1, j + 1) + k] * fx) * fy); return o.map((s) => s / 255); };
  const cen = V(centre);
  parts.forEach((part, pi) => {
    const P = part.pos, N = part.nrm, T = part.tri, sp = [], wn = [];
    for (let v = 0; v < P.length; v += 3) { const q = V([P[v], P[v + 1], P[v + 2]]), n = V([N[v], N[v + 1], N[v + 2]]); sp.push([(q[0] - cen[0]) * scale + w / 2, h / 2 - (q[1] - cen[1]) * scale, q[2] - cen[2]]); wn.push(n); }
    for (let t = 0; t < T.length; t += 3) {
      const a = T[t], b = T[t + 1], c = T[t + 2], A = sp[a], B = sp[b], C = sp[c], den = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]);
      if (den >= 0) continue;   // back face (screen y points down, so front faces have negative area)
      let tan = null; if (part.uv && part.normalMap) { const U = part.uv, e1 = [0, 1, 2].map((k) => P[b * 3 + k] - P[a * 3 + k]), e2 = [0, 1, 2].map((k) => P[c * 3 + k] - P[a * 3 + k]), du1 = U[b * 2] - U[a * 2], dv1 = U[b * 2 + 1] - U[a * 2 + 1], du2 = U[c * 2] - U[a * 2], dv2 = U[c * 2 + 1] - U[a * 2 + 1], r = du1 * dv2 - du2 * dv1 || 1e-12; tan = { T: V([0, 1, 2].map((k) => (e1[k] * dv2 - e2[k] * dv1) / r)), B: V([0, 1, 2].map((k) => (e2[k] * du1 - e1[k] * du2) / r)) }; }
      const x0 = Math.max(0, Math.floor(Math.min(A[0], B[0], C[0]))), x1 = Math.min(w - 1, Math.ceil(Math.max(A[0], B[0], C[0]))), y0 = Math.max(0, Math.floor(Math.min(A[1], B[1], C[1]))), y1 = Math.min(h - 1, Math.ceil(Math.max(A[1], B[1], C[1])));
      for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
        const px = x + 0.5, py = y + 0.5, l0 = ((B[1] - C[1]) * (px - C[0]) + (C[0] - B[0]) * (py - C[1])) / den, l1 = ((C[1] - A[1]) * (px - C[0]) + (A[0] - C[0]) * (py - C[1])) / den, l2 = 1 - l0 - l1;
        if (l0 < 0 || l1 < 0 || l2 < 0) continue;
        const z = l0 * A[2] + l1 * B[2] + l2 * C[2], o = y * w + x; if (z <= depth[o]) continue;
        let n = norm([0, 1, 2].map((k) => l0 * wn[a][k] + l1 * wn[b][k] + l2 * wn[c][k])), alb = part.color || [0.6, 0.5, 0.45], ao = 1, thin = 0;
        if (part.uv) { const u = l0 * part.uv[a * 2] + l1 * part.uv[b * 2] + l2 * part.uv[c * 2], v = l0 * part.uv[a * 2 + 1] + l1 * part.uv[b * 2 + 1] + l2 * part.uv[c * 2 + 1];
          if (part.albedo) alb = tex(part.albedo, u, v); if (part.ao) ao = tex(part.ao, u, v)[0]; if (part.thin) thin = tex(part.thin, u, v)[0];
          if (tan) { const m = tex(part.normalMap, u, v).map((s) => s * 2 - 1), tt = norm(tan.T.map((s, k) => s - n[k] * (s * n[k] + 0))), bb = tan.B; const Tn = norm(tan.T.map((s, k) => s - n[k] * (tan.T[0] * n[0] + tan.T[1] * n[1] + tan.T[2] * n[2]))); const Bn = [n[1] * Tn[2] - n[2] * Tn[1], n[2] * Tn[0] - n[0] * Tn[2], n[0] * Tn[1] - n[1] * Tn[0]]; const sg = Bn[0] * bb[0] + Bn[1] * bb[1] + Bn[2] * bb[2] < 0 ? -1 : 1; void tt; n = norm([0, 1, 2].map((k) => Tn[k] * m[0] + Bn[k] * sg * m[1] + n[k] * m[2])); } }
        else if (part.vcol) alb = [0, 1, 2].map((k) => l0 * part.vcol[a * 3 + k] + l1 * part.vcol[b * 3 + k] + l2 * part.vcol[c * 3 + k]);
        const dk = Math.max(0, n[0] * KEY[0] + n[1] * KEY[1] + n[2] * KEY[2]), df = Math.max(0, n[0] * FILL[0] + n[1] * FILL[1] + n[2] * FILL[2]), dr = Math.max(0, n[0] * RIM[0] + n[1] * RIM[1] + n[2] * RIM[2]);
        const step = (v) => { const q = Math.floor(v * bands + 0.5) / bands; return v * 0.4 + q * 0.6; }, lk = step(dk) * (0.35 + 0.65 * ao) , amb = 0.27 * ao;
        let r = alb[0] * (lk * 1.12 + amb + df * 0.1 * 0.8), g = alb[1] * (lk * 0.97 + amb + df * 0.1 * 0.8 * 0.9), bl = alb[2] * (lk * 0.8 + amb * 1.1 + df * 0.2);
        r += dr * 0.04 + thin * dk * 0.05; g += dr * 0.05; bl += dr * 0.07;
        if (part.spec) { const hv = norm([KEY[0], KEY[1], KEY[2] + 1]), s = Math.pow(Math.max(0, n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2]), part.spec); r += s * 0.9; g += s * 0.9; bl += s * 0.85; }
        col[o * 3] = r; col[o * 3 + 1] = g; col[o * 3 + 2] = bl; depth[o] = z; id[o] = pi;
      }
    }
  });
  // ink: silhouettes and depth breaks, heavier on the outside
  if (ink) for (let y = 1; y < h - 1; y++) for (let x = 1; x < w - 1; x++) {
    const o = y * w + x; if (id[o] < 0) continue;
    let edge = 0; for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const q = o + dy * w + dx; if (id[q] < 0) edge = Math.max(edge, 1); else if (Math.abs(depth[q] - depth[o]) > 0.006) edge = Math.max(edge, 0.6); else if (id[q] !== id[o]) edge = Math.max(edge, 0.3); }
    if (edge) { const k = 1 - 0.72 * edge * ink; col[o * 3] *= k; col[o * 3 + 1] *= k * 0.97; col[o * 3 + 2] *= k * 1.02; }
  }
  const out = new Uint8Array(W * H * 4);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { for (let k = 0; k < 3; k++) { let s = 0; for (let j = 0; j < ss; j++) for (let i = 0; i < ss; i++) s += col[((y * ss + j) * w + x * ss + i) * 3 + k]; out[(y * W + x) * 4 + k] = Math.round(255 * Math.max(0, Math.min(1, s / (ss * ss)))); } out[(y * W + x) * 4 + 3] = 255; }
  return { width: W, height: H, data: out };
}

export function hstack(imgs, gap = 4) {
  const H = Math.max(...imgs.map((i) => i.height)), W = imgs.reduce((s, i) => s + i.width, 0) + gap * (imgs.length - 1), data = new Uint8Array(W * H * 4).fill(30);
  for (let p = 3; p < data.length; p += 4) data[p] = 255;
  let x0 = 0; for (const im of imgs) { for (let y = 0; y < im.height; y++) data.set(im.data.subarray(y * im.width * 4, (y + 1) * im.width * 4), (y * W + x0) * 4); x0 += im.width + gap; }
  return { width: W, height: H, data };
}
export const upscale = (im, f) => { const W = im.width * f, H = im.height * f, data = new Uint8Array(W * H * 4); for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) data.set(im.data.subarray(((y / f | 0) * im.width + (x / f | 0)) * 4, ((y / f | 0) * im.width + (x / f | 0)) * 4 + 4), (y * W + x) * 4); return { width: W, height: H, data }; };
export const savePng = (im) => encodePng({ width: im.width, height: im.height, channels: 4, data: im.data });

// eyes: a sphere for the white, a cornea cap for the iris, pupil and catchlight on top, each its own part
function sphereMesh(c, r, nu = 28, nv = 18) {
  const pos = [], tri = [];
  for (let j = 0; j <= nv; j++) for (let i = 0; i <= nu; i++) { const th = j / nv * Math.PI, ph = i / nu * Math.PI * 2; pos.push(c[0] + r * Math.sin(th) * Math.sin(ph), c[1] + r * Math.cos(th), c[2] + r * Math.sin(th) * Math.cos(ph)); }
  for (let j = 0; j < nv; j++) for (let i = 0; i < nu; i++) { const a = j * (nu + 1) + i, b = a + 1, d = a + nu + 1, e = d + 1; tri.push(a, d, b, b, d, e); }
  return { pos: Float32Array.from(pos), tri: Uint32Array.from(tri) };
}
function capMesh(c, r, axis, ang, bulge = 0, seg = 24, rings = 4, at = null) {
  const a = norm(axis), t1 = Math.abs(a[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0], u = norm([t1[1] * a[2] - t1[2] * a[1], t1[2] * a[0] - t1[0] * a[2], t1[0] * a[1] - t1[1] * a[0]]), w = [a[1] * u[2] - a[2] * u[1], a[2] * u[0] - a[0] * u[2], a[0] * u[1] - a[1] * u[0]], pos = [], tri = [];
  const ctr = at || c;
  for (let j = 0; j <= rings; j++) for (let i = 0; i < seg; i++) { const th = ang * j / rings, ph = i / seg * Math.PI * 2, s = Math.sin(th), co = Math.cos(th), n = [0, 1, 2].map((k) => a[k] * co + (u[k] * Math.cos(ph) + w[k] * Math.sin(ph)) * s); pos.push(...[0, 1, 2].map((k) => ctr[k] + n[k] * (r + bulge))); }
  for (let j = 0; j < rings; j++) for (let i = 0; i < seg; i++) { const p = j * seg + i, q = j * seg + (i + 1) % seg, d = p + seg, e = q + seg; tri.push(p, d, q, q, d, e); }
  return { pos: Float32Array.from(pos), tri: Uint32Array.from(tri) };
}
export function eyeParts(S) {
  const out = [];
  for (const e of S.eyes || []) for (const mir of e.mirror ? [1, -1] : [1]) {
    const c = [e.c[0] * mir, e.c[1], e.c[2]], g = norm([e.gaze[0] * mir, e.gaze[1], e.gaze[2]]), ir = e.iris, ang = Math.asin(ir / e.r), fin = (m, name, color, spec) => { const nrm = new Float32Array(m.pos.length); for (let v = 0; v < m.pos.length; v += 3) { const n = norm([m.pos[v] - c[0], m.pos[v + 1] - c[1], m.pos[v + 2] - c[2]]); nrm.set(n, v); } return { ...m, nrm, name, color, spec, material: name }; };
    const bulge = e.bulge ?? 0.0007, rc = e.r * 0.62, cc = c.map((v, i) => v + g[i] * (e.r + bulge - rc));
    out.push(fin(sphereMesh(c, e.r), "eye_sclera", e.sclera || [0.82, 0.78, 0.7]));
    out.push(fin(capMesh(cc, rc * 1.002, g, Math.asin(Math.min(1, ir / rc)), 0, 28, 4), "eye_iris", e.irisColor || [0.36, 0.4, 0.42], 40));
    out.push(fin(capMesh(cc, rc * 1.004, g, Math.asin(Math.min(1, ir * 0.42 / rc)), 0, 20, 2), "eye_pupil", [0.03, 0.025, 0.03], 60));
    const cd = norm([g[0] * 0.9 - 0.25 * mir, g[1] + 0.38, g[2]]);
    out.push(fin(capMesh(cc, rc * 1.006, cd, 0.27, 0, 10, 2), "eye_catch", [0.95, 0.93, 0.88], 0));
  }
  return out;
}
