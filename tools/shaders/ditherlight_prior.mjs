// The model prediction of the surface-lock metric L (ditherlight_metrics.mjs, predictL), from the
// motion field and the tone of the first frame pair of each path. No threshold is read and no
// output level is computed. Writes the numbers the protocol quotes.
//   node tools/shaders/ditherlight_prior.mjs [--out evidence/ditherlight-motion-prediction.json]
import { writeFileSync } from "node:fs";
import { pathSpecs, lightsOf } from "../../web/shaders/fixtures/dream.mjs";
import { resolveDitherlight, toneOf } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { renderFrames } from "./ditherlight_pool.mjs";
import { reproject, predictL, validCount } from "./ditherlight_metrics.mjs";

export const W = 256, H = 144, PATHS = ["dolly", "strafe", "yaw", "orbit", "spin"], ANCHORS = ["screen", "world-fixed", "world-octave"];

export function withTone(plan, spec, g) {
  const lights = lightsOf(spec), px = { vis: [0, 0, 0, 0], wpos: [0, 0, 0], nrm: [0, 0, 1] };
  g.tone = new Float32Array(g.width * g.height);
  for (let i = 0; i < g.tone.length; i++) {
    px.kind = g.kind[i]; px.albedo = g.albedo[i]; px.depth = g.depth[i];
    for (let c = 0; c < 3; c++) { px.wpos[c] = g.wpos[i * 3 + c]; px.nrm[c] = g.nrm[i * 3 + c]; }
    for (let c = 0; c < 4; c++) px.vis[c] = g.vis[i * 4 + c];
    g.tone[i] = toneOf(plan, lights, px);
  }
  return g;
}

if ((process.argv[1] || "").endsWith("ditherlight_prior.mjs")) {
  const res = {};
  for (const path of PATHS) {
    const specs = pathSpecs(path, 2), [g0, g1] = await renderFrames(specs, W, H), out = {};
    const plan0 = resolveDitherlight("ink", {}, { height: H });
    withTone(plan0, specs[1], g1);
    const r = reproject(g0, g1);
    let n = 0, ind = 0; for (let i = 0; i < r.q.length; i++) if (r.q[i] >= 0) { n++; ind += 2 * g1.tone[i] * (1 - g1.tone[i]); }
    out.valid_fraction = validCount(r) / (W * H); out.independent_2f1mf = ind / n;
    for (const a of ANCHORS) out[a] = predictL(resolveDitherlight("ink", { anchor: a }, { height: H }), g1, r, a);
    res[path] = out;
    console.log(path, JSON.stringify(out));
  }
  const o = process.argv.indexOf("--out");
  if (o >= 0) writeFileSync(process.argv[o + 1], JSON.stringify({ what: "model prediction of L from the motion field and tone of frame pair (0,1) of each path; no threshold read", size: [W, H], paths: res }, null, 1) + "\n");
}
