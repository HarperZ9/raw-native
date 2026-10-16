// RT visibility through the dither threshold: runs Q1 to Q4 of
// project-docs/RT-DITHER-VISIBILITY-PROTOCOL.md (fixed in evidence/rtvis-protocol.json before this
// tool existed) on Penumbra Yard and writes evidence/rtvis-study-runs.json. Q0 is a unit test.
//   node tools/shaders/rtvis_study.mjs [--out evidence/rtvis-study-runs.json] [--stills DIR]
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { visibility } from "../../web/shaders/ditherlight/rtvis.mjs";
import { YARD, yardLight, BLOCKERS, occludedBy, yardPoint, yardTone } from "../../web/shaders/fixtures/penumbra.mjs";
import { resolveDitherlight, quantise, levelLinear } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { thresholdAt } from "../../web/shaders/ditherlight/thresholds.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), arg = (k, d) => (args.includes(k) ? args[args.indexOf(k) + 1] : d);
const proto = JSON.parse(readFileSync("evidence/rtvis-protocol.json", "utf8")), N = YARD.px, T = proto.penumbra_tile.tile, [vlo, vhi] = proto.penumbra_tile.vis_open_interval;
const stills = arg("--stills", null); if (stills) mkdirSync(stills, { recursive: true });
// --extra MODULE registers exploratory methods in METHODS of rtvis.mjs; --methods and --blockers narrow a run.
if (arg("--extra", null)) await import(pathToFileURL(arg("--extra")).href);
const METHODS = arg("--methods", "ref64,white1,rank1,net1").split(",");
const BLOCKS = arg("--blockers", proto.scene.blockers.join(",")).split(",");

function visMap(method, blocker, light, frame = 0) {
  const occ = occludedBy(BLOCKERS[blocker]), V = new Float64Array(N * N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) V[y * N + x] = visibility(method, light, yardPoint(x, y), x, y, frame, occ);
  return V;
}
function levels(plan, V, light) {
  const L = new Uint8Array(N * N);
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) L[y * N + x] = quantise(plan, yardTone(yardPoint(x, y), light, V[y * N + x]), thresholdAt("bayer8", x, y));
  return L;
}
function tileStats(plan, L, truthV, light) {
  let n = 0, signed = 0, abs = 0;
  for (let ty = 0; ty < N / T; ty++) for (let tx = 0; tx < N / T; tx++) {
    let pen = false, mo = 0, mt = 0;
    for (let y = ty * T; y < ty * T + T; y++) for (let x = tx * T; x < tx * T + T; x++) {
      const i = y * N + x, v = truthV[i]; if (v > vlo && v < vhi) pen = true;
      mo += levelLinear(plan, L[i]); mt += yardTone(yardPoint(x, y), light, v);
    }
    if (!pen) continue;
    const e = (mo - mt) / (T * T); n++; signed += e; abs += Math.abs(e);
  }
  return { tiles: n, bias: signed / n, mae: abs / n };
}
const penMask = (V) => Array.from(V, (v) => v > vlo && v < vhi);
const changed = (A, B, mask) => { let c = 0, m = 0; for (let i = 0; i < A.length; i++) if (mask[i]) { m++; if (A[i] !== B[i]) c++; } return c / m; };

const res = { protocol: "evidence/rtvis-protocol.json", date: new Date().toISOString().slice(0, 10), cases: [] }, t0 = Date.now();
for (const blocker of BLOCKS) {
  const light = yardLight(), truth = visMap("truth", blocker, light), maps = Object.fromEntries(METHODS.map((m) => [m, visMap(m, blocker, light)]));
  for (const lv of proto.scene.levels) {
    const plan = resolveDitherlight("ink", { levels: lv }), row = { blocker, levels: lv };
    for (const m of METHODS) {
      const L = levels(plan, maps[m], light); row[m] = tileStats(plan, L, truth, light);
      if (stills) { const img = new Uint8ClampedArray(N * N * 4); for (let i = 0; i < N * N; i++) { const g = Math.round(255 * Math.pow(levelLinear(plan, L[i]), 1 / 2.2)); img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = g; img[i * 4 + 3] = 255; } writePng(`${stills}/${blocker}-${lv}-${m}.png`, { width: N, height: N, data: img }); }
    }
    res.cases.push(row);
    console.log(blocker, lv, METHODS.map((m) => `${m} bias ${row[m].bias.toFixed(4)} mae ${row[m].mae.toFixed(4)}`).join(" | "), `(${row.net1.tiles} tiles)`);
  }
}
// Q3 temporal, on the wall-x yard, 1 bit
if (!args.includes("--no-q3")) {
  const plan = resolveDitherlight("ink", { levels: 2 }), b = "wall-x", L0 = yardLight(), mask = penMask(visMap("truth", b, L0));
  const stat = (m) => { const F = Array.from({ length: proto.Q3_temporal.static_frames }, (_, f) => levels(plan, visMap(m, b, L0, f), L0)); let s = 0; for (let f = 1; f < F.length; f++) s += changed(F[f - 1], F[f], mask); return s / (F.length - 1); };
  const mov = (m) => { const st = proto.Q3_temporal.moving; let s = 0, prev = null; for (let f = 0; f < st.frames; f++) { const L = yardLight(st.step_m * f), cur = levels(plan, visMap(m, b, L, f), L); if (prev) s += changed(prev, cur, mask); prev = cur; } return s / (st.frames - 1); };
  res.Q3 = { static: { net1: stat("net1"), white1: stat("white1") }, moving: { net1: mov("net1"), ref64: mov("ref64") } };
  console.log("Q3", JSON.stringify(res.Q3));
}
const q1 = proto.Q1_bias, q2 = proto.Q2_accuracy, q3 = proto.Q3_temporal;
if (res.Q3 && METHODS.includes("rank1")) res.verdicts = {
  Q1_net1_white1: res.cases.every((c) => Math.abs(c.net1.bias) <= q1.net1_white1_abs_max && Math.abs(c.white1.bias) <= q1.net1_white1_abs_max),
  Q1_rank1_biased_somewhere: res.cases.some((c) => c.blocker.startsWith("wall") && Math.abs(c.rank1.bias) >= q1.rank1_abs_min_in_some_wall),
  Q2_net1_vs_white1: res.cases.every((c) => c.net1.mae <= q2.net1_over_white1_max * c.white1.mae),
  Q2_net1_vs_ref64: res.cases.every((c) => c.net1.mae <= q2.net1_over_ref64_max * c.ref64.mae),
  Q3_static: res.Q3.static.net1 === q3.net1_changes && res.Q3.static.white1 >= q3.white1_min_fraction,
  Q3_moving: res.Q3.moving.net1 <= q3.moving.net1_over_ref64_max * res.Q3.moving.ref64,
  Q4_rays: { net1: 1, ref64: 64 },
};
res.seconds = (Date.now() - t0) / 1000;
writeFileSync(arg("--out", "evidence/rtvis-study-runs.json"), JSON.stringify(res, null, 1) + "\n");
console.log("verdicts", JSON.stringify(res.verdicts));
