// Shared steps of the paint-over measurement tools.
import { isoCamera } from "../../web/shaders/paintover/camera.mjs";
import { makeProjector, covered, shadePoint, renderProjected, stretchMap, DEFAULTS } from "../../web/shaders/paintover/projector.mjs";
import { paint } from "../../web/shaders/paintover/painter.mjs";
import { renderGbufs } from "./paintover_pool.mjs";

export const TARGET = [1, 1.5, 0], SCALE = 9, PW = 960, PH = 540, VW = 480, VH = 270;
export const camAt = (dyaw, delev = 0) => isoCamera({ yawDeg: 45 + dyaw, elevDeg: 35.264 + delev, target: TARGET, scale: SCALE });

// Paint over a render made from cam; returns the projector and the lit export.
export function paintProjector(cam, gShaded, seed) {
  const painting = paint({ w: PW, h: PH, rgb: gShaded.shade }, { id: gShaded.id, N: gShaded.N }, { seed });
  return { projector: makeProjector(cam, PW, PH, painting.rgba8, gShaded.depth), painting };
}
export const codes = (rgb) => Math.round(Math.min(1, Math.max(0, rgb)) * 255);

// Per-view analysis against a projector list. primary = projectors[0] drives the stretch.
export function analyseView(projectors, g, viewCam) {
  const n = g.w * g.h, P = [0, 0, 0], N = [0, 0, 0];
  let hit = 0, holes = 0, blend = 0;
  for (let i = 0; i < n; i++) {
    if (!g.hit[i]) continue;
    hit++;
    for (let c = 0; c < 3; c++) { P[c] = g.P[i * 3 + c]; N[c] = g.N[i * 3 + c]; }
    let k = 0; for (const pr of projectors) if (covered(pr, P, N)) k++;
    if (k === 0) holes++; else if (k > 1) blend++;
  }
  return { hit, holes: holes / hit, blend: blend / hit };
}
const pct = (a, p) => { if (!a.length) return NaN; const s = Float64Array.from(a).sort(); return s[Math.min(s.length - 1, Math.floor(p * s.length))]; };
export function stretchStats(primary, g, ids) {
  const { smax, smin } = stretchMap(primary, g), k = g.h / PH, out = {};
  for (const [name, id] of Object.entries(ids)) {
    const M = [], A = [];
    for (let i = 0; i < g.w * g.h; i++) {
      if (g.id[i] !== id || !(smin[i] > 1e-6)) continue;
      const P = [g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]], N = [g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]];
      if (!covered(primary, P, N)) continue;
      M.push(1 / (smin[i] * k)); A.push(smax[i] / smin[i]);
    }
    out[name] = { n: M.length, M_mean: M.reduce((a, b) => a + b, 0) / (M.length || 1), M_p95: pct(M, 0.95), A_mean: A.reduce((a, b) => a + b, 0) / (A.length || 1), A_p95: pct(A, 0.95) };
  }
  return out;
}
export { renderGbufs, covered, shadePoint, renderProjected, makeProjector, DEFAULTS };
