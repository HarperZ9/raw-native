// EXPLORATORY (post hoc, after the preregistered off-axis run; decides nothing): what are the
// primary-only holes? Splits them into outside the painting frame, facing away (f below the
// minimum), and hidden behind nearer geometry.
//   node tools/shaders/paintover_holes_explore.mjs --out evidence/paintover-holes-exploratory.json
import { writeFileSync } from "node:fs";
import { camAt, PW, PH, VW, VH, paintProjector, renderGbufs } from "./paintover_lib.mjs";
import { project, toCamera, dot, pixelWorld } from "../../web/shaders/paintover/camera.mjs";
import { DEFAULTS } from "../../web/shaders/paintover/projector.mjs";

const yaws = [0, 5, 10, 20, 30, 45, 60, 90];
const gs = await renderGbufs([{ cam: camAt(0), w: PW, h: PH }, ...yaws.map((y) => ({ cam: camAt(y), w: VW, h: VH, shadeOn: false }))]);
const { projector: pr } = paintProjector(camAt(0), gs[0], 7), res = {};
yaws.forEach((y, k) => {
  const g = gs[k + 1], c = { hit: 0, frame: 0, facing: 0, occluded: 0 };
  for (let i = 0; i < g.w * g.h; i++) {
    if (!g.hit[i]) continue;
    c.hit++;
    const P = [g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]], N = [g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]], p = project(pr.cam, PW, PH, P);
    if (!(p[0] >= -0.5 && p[0] <= PW - 0.5 && p[1] >= -0.5 && p[1] <= PH - 0.5)) { c.frame++; continue; }
    const f = dot(N, toCamera(pr.cam, P)); if (f < DEFAULTS.fMin) { c.facing++; continue; }
    const dm = pr.depth[Math.round(p[1]) * PW + Math.round(p[0])], tol = pixelWorld(pr.cam, PH, p[2]) * (DEFAULTS.tolBase + DEFAULTS.tolGrazing * (1 - f));
    if (p[2] - dm > tol) c.occluded++;
  }
  res[`yaw${y}`] = { frame: c.frame / c.hit, facing: c.facing / c.hit, occluded: c.occluded / c.hit };
  console.log(`yaw${y}`, JSON.stringify(res[`yaw${y}`]));
});
const o = process.argv.indexOf("--out"); if (o >= 0) writeFileSync(process.argv[o + 1], JSON.stringify({ exploratory: true, note: "post hoc; decides nothing", holes: res }, null, 1) + "\n");
