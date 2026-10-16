// The analytic stretch of a planar surface under paint-over projection, from the cameras alone
// (no render, no painting): the ground plane y = 0, seen by an orthographic view of the same
// elevation rotated by theta about the vertical, against the primary projector. The map is affine,
// so one number per theta. Texel magnification M = 1 / (sigma_min x view height / painting height),
// anisotropy A = sigma_max / sigma_min.
//   node tools/shaders/paintover_prior.mjs [--out evidence/paintover-offaxis-prediction.json]
import { writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { isoCamera, ray, project } from "../../web/shaders/paintover/camera.mjs";

export const VIEW = { w: 480, h: 270 }, PAINT = { w: 960, h: 540 };
export const TARGET = [1, 1.5, 0], SCALE = 9;
export const primaryCam = () => isoCamera({ yawDeg: 45, target: TARGET, scale: SCALE });
export const viewCam = (dyaw, delev = 0) => isoCamera({ yawDeg: 45 + dyaw, elevDeg: 35.264 + delev, target: TARGET, scale: SCALE });

export function groundStretch(dyaw, delev = 0) {
  const prim = primaryCam(), view = viewCam(dyaw, delev), at = (x, y) => { const r = ray(view, VIEW.w, VIEW.h, x, y), t = -r.o[1] / r.d[1]; return project(prim, PAINT.w, PAINT.h, [r.o[0] + r.d[0] * t, 0, r.o[2] + r.d[2] * t]); };
  const a = at(240, 135), bx = at(241, 135), by = at(240, 136), j00 = bx[0] - a[0], j10 = bx[1] - a[1], j01 = by[0] - a[0], j11 = by[1] - a[1];
  const s1 = j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11, det = j00 * j11 - j01 * j10, s2 = Math.sqrt(Math.max(0, s1 * s1 - 4 * det * det)), smax = Math.sqrt((s1 + s2) / 2), smin = Math.sqrt(Math.max(1e-12, (s1 - s2) / 2)), k = VIEW.h / PAINT.h;
  return { smax, smin, magnification: 1 / (smin * k), anisotropy: smax / smin, area: Math.abs(det) };
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const out = {};
  for (const th of [0, 5, 10, 20, 30, 45, 60]) out[`yaw${th}`] = groundStretch(th);
  for (const de of [-10, 10]) out[`elev${de}`] = groundStretch(0, de);
  for (const [k, v] of Object.entries(out)) console.log(k, `M ${v.magnification.toFixed(3)} A ${v.anisotropy.toFixed(3)} area ${v.area.toFixed(3)}`);
  const o = process.argv.indexOf("--out");
  if (o >= 0) writeFileSync(process.argv[o + 1], JSON.stringify({ what: "analytic ground-plane stretch of the primary projector under off-axis views, from the cameras alone", view: VIEW, painting: PAINT, ground: out }, null, 1) + "\n");
}
