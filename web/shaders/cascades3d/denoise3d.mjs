// Denoise for the screen probes of the 3D cascades, CPU float64 (wgsl3d-denoise.mjs is the GPU side).
// Three parts, each switched by the plan (math3d.mjs planCascades3d):
// - temporal (tf > 0): every frame the probes move inside their stride cell (Halton 2, 3) and their
//   rays take a new seed; each pixel's irradiance is blended with its history, reprojected through the
//   previous frame's camera and kept only where the previous surface matches (plane distance and
//   normal). At most tmax frames of history.
// - spatial (sf > 0): sf a-trous passes (steps 1, 2, 4 ...) of a 3 x 3 [1 2 1] / 4 kernel over the
//   probe grid, with the upsampling's plane and normal test, and a luminance edge stop when sl > 0.
// - camera motion (move = { d: [x, y, z], k }): the camera starts d away and slides back over the
//   last k frames, ending on the scene camera (the reprojection test).
import { hash32, dot, sub, add, mul } from "./math3d.mjs";

const LUM = [0.2126, 0.7152, 0.0722];
const lum = (e) => LUM[0] * e[0] + LUM[1] * e[1] + LUM[2] * e[2];
const halton = (i, b) => { let r = 0; for (let s = 1 / b; i > 0; i = Math.floor(i / b), s /= b) r += s * (i % b); return r; };

// Frame f's ray seed (xor-ed into the probe index of the jitter hash): 0 without temporal.
export function frameSeed(plan, f) { return plan.tf > 0 ? hash32(f + 1) : 0; }
// Frame f's probe offsets inside the stride cell: the centre without temporal.
export function probeOffsets(plan, f) {
  const k = plan.stride;
  if (!(plan.tf > 0)) return [k >> 1, k >> 1];
  return [Math.min(k - 1, Math.floor(halton(f + 1, 2) * k)), Math.min(k - 1, Math.floor(halton(f + 1, 3) * k))];
}
// The camera of frame f.
export function cameraAt(sc, plan, f) {
  const mv = plan.move;
  if (!mv) return sc.camera;
  const a = Math.min(1, Math.max(0, (plan.frames - 1 - f) / mv.k)), d = mv.d.map((v) => v * a);
  return { ...sc.camera, eye: add(sc.camera.eye, d), target: add(sc.camera.target, d) };
}
// World size of one pixel at x, seen from camera basis kb.
export const footprint = (kb, p, H) => Math.hypot(...sub(p, kb.eye)) * 2 * kb.th / H;

// Probe weight of q seen from probe or pixel surface x (the upsampling test of run3d.mjs).
function planeNormal(x, q, tol) {
  const n = dot(x.n, q.n);
  return n > 0.5 && Math.abs(dot(sub(q.p, x.p), x.ng)) <= tol ? n * n * n * n : 0;
}

// One a-trous pass of step s over the screen probes SP = { g, E: [{ e, p, n, ng } | null] }.
export function atrousPass(plan, kb, SP, s, H) {
  const { g, E } = SP, k = plan.stride, out = new Array(E.length).fill(null), h = [0.25, 0.5, 0.25];
  for (let j = 0; j < g.ph; j++) for (let i = 0; i < g.pw; i++) {
    const c = E[j * g.pw + i];
    if (!c) continue;
    const tol = 4 * k * s * footprint(kb, c.p, H) + 1e-3, lc = lum(c.e), acc = [0, 0, 0];
    let ws = 0;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
      const ii = i + dx * s, jj = j + dy * s;
      if (ii < 0 || jj < 0 || ii >= g.pw || jj >= g.ph) continue;
      const q = E[jj * g.pw + ii];
      if (!q) continue;
      let w = h[dx + 1] * h[dy + 1] * planeNormal(c, q, tol);
      if (w > 0 && plan.sl > 0) { const r = Math.abs(lum(q.e) - lc) / Math.max(lc, lum(q.e), 1e-6); w *= Math.exp(-(r / plan.sl) * (r / plan.sl)); }
      if (w <= 0) continue;
      for (let ch = 0; ch < 3; ch++) acc[ch] += w * q.e[ch];
      ws += w;
    }
    out[j * g.pw + i] = { ...c, e: acc.map((v) => v / ws) };
  }
  return { g, E: out };
}
export function atrous(plan, kb, SP, H) {
  for (let p = 0; p < plan.sf; p++) SP = atrousPass(plan, kb, SP, 1 << p, H);
  return SP;
}

// Pixel x's history from the previous frame: bilinear over the 4 pixels around its reprojection,
// each kept when its surface lies on x's plane (4 pixel footprints) with a normal within 0.9.
// Returns { e, n } or null.
export function reproject(prev, W, H, X, kb) {
  const { kb: k, G, hist, count } = prev, v = sub(X.p, k.eye), z = dot(v, k.f);
  if (z <= 0) return null;
  const fx = (dot(v, k.s) / (z * k.th * k.aspect) + 1) * 0.5 * W - 0.5, fy = (1 - dot(v, k.u) / (z * k.th)) * 0.5 * H - 0.5;
  const x0 = Math.floor(fx), y0 = Math.floor(fy), ax = fx - x0, ay = fy - y0, tol = 4 * footprint(kb, X.p, H) + 1e-3;
  const e = [0, 0, 0];
  let ws = 0, n = 0;
  for (let c = 0; c < 4; c++) {
    const x = x0 + (c & 1), y = y0 + (c >> 1);
    if (x < 0 || y < 0 || x >= W || y >= H) continue;
    const w = ((c & 1) ? ax : 1 - ax) * ((c >> 1) ? ay : 1 - ay), q = G[y * W + x];
    if (w <= 0 || !q || dot(X.n, q.x.n) < 0.9 || Math.abs(dot(sub(q.x.p, X.p), X.ng)) > tol) continue;
    const o = y * W + x;
    for (let ch = 0; ch < 3; ch++) e[ch] += w * hist[3 * o + ch];
    n += w * count[o]; ws += w;
  }
  return ws > 1e-3 ? { e: mul(e, 1 / ws), n: n / ws } : null;
}

// Blend this frame's irradiance Ecur (array of [r, g, b] | null a pixel) into the history.
export function accumulate(plan, G, W, H, Ecur, kb, prev) {
  const hist = new Float64Array(3 * W * H), count = new Float64Array(W * H);
  for (let p = 0; p < W * H; p++) {
    const s = G[p];
    if (!s) continue;
    let e = Ecur[p], n = 1;
    const h = prev ? reproject(prev, W, H, s.x, kb) : null;
    if (h) { n = Math.min(h.n + 1, plan.tmax); e = add(h.e, mul(sub(e, h.e), 1 / n)); }
    hist.set(e, 3 * p); count[p] = n;
  }
  return { kb, G, hist, count };
}
