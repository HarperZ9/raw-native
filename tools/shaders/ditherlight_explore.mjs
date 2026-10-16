// EXPLORATORY (post hoc, after the preregistered run; it decides nothing).
// Diagnoses two things the preregistered run left open, with the sky shards frozen (time 0):
//  1. the static control: F was 0.07, not 0; is the drifting sky the cause?
//  2. the world-octave target: the preregistered run used 2 px cells (2 to 4 px after the octave
//     snap) and paid a large accuracy cost; what do 1 px and 4 px targets give?
//   node tools/shaders/ditherlight_explore.mjs --out evidence/ditherlight-motion-exploratory.json
import { writeFileSync } from "node:fs";
import { pathSpecs, lightsOf } from "../../web/shaders/fixtures/dream.mjs";
import { resolveDitherlight, runDitherlight, levelLinear } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { renderFrames } from "./ditherlight_pool.mjs";
import { reproject, surfaceLock, errorImage, boil, accuracy } from "./ditherlight_metrics.mjs";
import { W, H } from "./ditherlight_prior.mjs";

const NF = 32, mean = (a) => a.reduce((s, v) => s + v, 0) / a.length, res = {};
const variants = [["bayer8", "screen", {}], ["bayer8", "world-fixed", {}], ["bayer8", "world-octave", { px: 1 }], ["bayer8", "world-octave", { px: 2 }], ["bayer8", "world-octave", { px: 4 }], ["blue64", "screen", {}], ["blue64", "world-octave", { px: 1 }]];
for (const path of ["static", "dolly", "strafe", "yaw"]) {
  const specs = pathSpecs(path, NF).map((s) => ({ ...s, time: 0 })), gs = await renderFrames(specs, W, H);
  res[path] = {};
  for (const [source, anchor, ov] of variants) {
    const plan = resolveDitherlight("ink", { source, anchor, ...ov }, { height: H }), runs = gs.map((g, i) => runDitherlight(plan, lightsOf(specs[i]), g));
    const E = runs.map((r) => errorImage(Float32Array.from(r.level, (k) => levelLinear(plan, k)), r.tone, W, H)), m = { L: [], F: [], A: [] };
    for (let t = 1; t < NF; t++) { const r = reproject(gs[t - 1], gs[t]); m.L.push(surfaceLock(r, runs[t - 1].level, runs[t].level)); m.F.push(boil(E[t - 1], E[t])); m.A.push(accuracy(E[t])); }
    res[path][`${source}/${anchor}${ov.px ? `/px${ov.px}` : ""}`] = { L: mean(m.L), F: mean(m.F), A: mean(m.A) };
  }
  console.log(path, JSON.stringify(res[path]));
}
const o = process.argv.indexOf("--out");
if (o >= 0) writeFileSync(process.argv[o + 1], JSON.stringify({ exploratory: true, note: "post hoc, sky frozen; decides nothing", results: res }, null, 1) + "\n");
