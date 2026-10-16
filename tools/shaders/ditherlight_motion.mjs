// Runs the dither-lighting motion comparison of project-docs/DITHER-LIGHTING-PROTOCOL.md.
//   node tools/shaders/ditherlight_motion.mjs --out evidence/ditherlight-motion-runs.json [--stills DIR]
import { writeFileSync, mkdirSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { pathSpecs, lightsOf } from "../../web/shaders/fixtures/dream.mjs";
import { resolveDitherlight, runDitherlight, levelLinear } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { renderFrames } from "./ditherlight_pool.mjs";
import { reproject, surfaceLock, errorImage, boil, accuracy, seam } from "./ditherlight_metrics.mjs";
import { W, H } from "./ditherlight_prior.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const proto = JSON.parse(readFileSync(new URL("../../evidence/ditherlight-motion-protocol.json", import.meta.url)));
const NF = proto.frames, CELLS = proto.cells, PATHS = [...proto.paths, proto.control_path];
const mean = (a) => a.reduce((s, v) => s + v, 0) / a.length;

function displaced(g0, g1, r) {
  const q = new Int32Array(r.q.length).fill(-1);
  for (let i = 0; i < q.length; i++) { const x = i % W; if (g1.kind[i] === 2 && x + 3 < W && g0.kind[i + 3] === 2) q[i] = i + 3; }
  return { q };
}

const results = {}, stills = {};
for (const path of PATHS) {
  const specs = pathSpecs(path, NF), gs = await renderFrames(specs, W, H), pairs = [];
  results[path] = {};
  for (const [source, anchor] of CELLS) {
    const plan = resolveDitherlight("ink", { source, anchor }, { height: H }), runs = gs.map((g, i) => runDitherlight(plan, lightsOf(specs[i]), g));
    const outLin = runs.map((r) => Float32Array.from(r.level, (k) => levelLinear(plan, k))), E = runs.map((r, i) => errorImage(outLin[i], r.tone, W, H));
    const m = { L: [], F: [], A: [], S: [], Ld: [] };
    for (let t = 1; t < NF; t++) {
      const r = reproject(gs[t - 1], gs[t]);
      m.L.push(surfaceLock(r, runs[t - 1].level, runs[t].level)); m.F.push(boil(E[t - 1], E[t])); m.A.push(accuracy(E[t]));
      m.Ld.push(surfaceLock(displaced(gs[t - 1], gs[t], r), runs[t - 1].level, runs[t].level));
      if (anchor === "world-octave") m.S.push(seam(plan, gs[t - 1], gs[t], r));
    }
    results[path][`${source}/${anchor}`] = { L: mean(m.L), F: mean(m.F), A: mean(m.A), S: m.S.length ? mean(m.S) : null, L_displaced: mean(m.Ld) };
    if (path === "dolly" && !args.includes("--no-stills")) stills[`${source}/${anchor}`] = [runs[10].rgba, runs[11].rgba];
  }
  console.log(path, JSON.stringify(results[path]));
}

// ---- checks against the predictions ----
const pred = JSON.parse(readFileSync(new URL("../../evidence/ditherlight-motion-prediction.json", import.meta.url))).paths;
const key = (s, a) => `${s}/${a}`, checks = [];
const add = (id, pass, detail) => { checks.push({ id, pass, detail }); console.log(pass ? "ok  " : "MISS", id, detail); };
for (const path of ["dolly", "strafe", "yaw", "orbit"]) for (const [s, a] of CELLS) {
  const L = results[path][key(s, a)].L, p = pred[path][a], tol = Math.max(0.35 * p, 0.02);
  add(`P1 ${path} ${s}/${a}`, Math.abs(L - p) <= tol, `L ${L.toFixed(4)} model ${p.toFixed(4)} tol ${tol.toFixed(4)}`);
}
for (const [s, a] of CELLS) add(`P1 spin ${s}/${a}`, results.spin[key(s, a)].L <= 0.01, `L ${results.spin[key(s, a)].L.toFixed(5)}`);
for (const s of ["bayer8", "blue64"]) {
  for (const path of ["dolly", "strafe", "yaw"]) {
    const R = (a) => results[path][key(s, a)];
    add(`P2 ${path} ${s}`, R("screen").L > R("world-fixed").L && R("world-fixed").L > R("world-octave").L, `L ${R("screen").L.toFixed(3)} > ${R("world-fixed").L.toFixed(3)} > ${R("world-octave").L.toFixed(3)}`);
    add(`P3 ${path} ${s}`, R("world-fixed").F >= 1.3 * R("screen").F && R("world-octave").F >= R("screen").F, `F screen ${R("screen").F.toFixed(3)} fixed ${R("world-fixed").F.toFixed(3)} octave ${R("world-octave").F.toFixed(3)}`);
  }
  const R = (a) => results.orbit[key(s, a)];
  add(`P2 orbit ${s}`, R("world-octave").L < R("world-fixed").L && R("world-octave").L < R("screen").L, `octave lowest: ${R("world-octave").L.toFixed(3)}`);
}
for (const path of ["dolly", "strafe", "yaw", "orbit", "spin"]) for (const a of ["screen", "world-fixed", "world-octave"]) {
  const b = results[path][key("bayer8", a)], n = results[path][key("blue64", a)];
  if (path !== "spin") add(`P4 L ${path} ${a}`, Math.abs(b.L - n.L) / Math.max(b.L, n.L) <= 0.1, `bayer ${b.L.toFixed(4)} blue ${n.L.toFixed(4)}`);
  add(`P4 F ${path} ${a}`, b.F <= n.F, `bayer ${b.F.toFixed(3)} blue ${n.F.toFixed(3)}`);
  add(`P4 A ${path} ${a}`, Math.abs(b.A - n.A) / Math.max(b.A, n.A) <= 0.15, `bayer ${b.A.toFixed(3)} blue ${n.A.toFixed(3)}`);
}
for (const [s, a] of CELLS) { const r = results.static[key(s, a)]; add(`P5 static ${s}/${a}`, r.L === 0 && r.F === 0, `L ${r.L} F ${r.F}`); }
for (const path of ["dolly", "strafe", "yaw"]) {
  const ind = pred[path].independent_2f1mf, r = results[path]["bayer8/screen"], r2 = results[path]["blue64/screen"];
  add(`P5 displaced ${path}`, [r, r2].every((x) => x.L_displaced >= 0.7 * ind && x.L_displaced <= 1.1 * ind), `bayer ${r.L_displaced.toFixed(3)} blue ${r2.L_displaced.toFixed(3)} independent ${ind.toFixed(3)}`);
}
for (const s of ["bayer8", "blue64"]) {
  const d = results.dolly[key(s, "world-octave")].S;
  add(`P6 dolly ${s}`, d >= 0.002 && d <= 0.03, `S ${d.toFixed(4)}`);
  for (const path of ["strafe", "yaw"]) add(`P6 ${path} ${s}`, results[path][key(s, "world-octave")].S <= 0.01, `S ${results[path][key(s, "world-octave")].S.toFixed(4)}`);
}

// ---- decision ----
const MOVING = proto.paths, score = {}, accuracyMean = {};
for (const [s, a] of CELLS) { score[key(s, a)] = []; accuracyMean[key(s, a)] = mean(MOVING.map((p) => results[p][key(s, a)].A)); }
for (const p of MOVING) {
  const Lmax = Math.max(...CELLS.map(([s, a]) => results[p][key(s, a)].L)), Fmax = Math.max(...CELLS.map(([s, a]) => results[p][key(s, a)].F));
  for (const [s, a] of CELLS) { const r = results[p][key(s, a)]; score[key(s, a)].push(Math.max(Lmax > 0 ? r.L / Lmax : 0, Fmax > 0 ? r.F / Fmax : 0)); }
}
const cellScore = Object.fromEntries(Object.entries(score).map(([k, v]) => [k, mean(v)])), bestA = Math.min(...Object.values(accuracyMean));
const eligible = Object.keys(cellScore).filter((k) => accuracyMean[k] <= 1.1 * bestA).sort((x, y) => cellScore[x] - cellScore[y]);
const meanOf = (s, f) => mean(MOVING.flatMap((p) => ["screen", "world-fixed", "world-octave"].map((a) => results[p][key(s, a)][f])));
const dL = Math.abs(meanOf("bayer8", "L") - meanOf("blue64", "L")) / Math.max(meanOf("bayer8", "L"), meanOf("blue64", "L")), dF = Math.abs(meanOf("bayer8", "F") - meanOf("blue64", "F")) / Math.max(meanOf("bayer8", "F"), meanOf("blue64", "F"));
const winnerAnchor = eligible.length ? eligible[0].split("/")[1] : null;
const sourceNotSeparated = dL < 0.1 && dF < 0.1;
const anchorByScoreBayer = ["screen", "world-fixed", "world-octave"].sort((x, y) => cellScore[key("bayer8", x)] - cellScore[key("blue64", y)])[0];
void anchorByScoreBayer;
const srcScore = (s) => mean(["screen", "world-fixed", "world-octave"].map((a) => cellScore[key(s, a)]));
const sourceChoice = sourceNotSeparated ? "bayer8 (cost choice: not separated by the metric)" : srcScore("bayer8") <= srcScore("blue64") ? "bayer8" : "blue64";
const bestCellPerSource = (s) => ["screen", "world-fixed", "world-octave"].sort((x, y) => cellScore[key(s, x)] - cellScore[key(s, y)])[0];
const anchorChoice = bestCellPerSource(sourceChoice.startsWith("bayer8") ? "bayer8" : "blue64");
const decision = { cellScore, accuracyMean, eligible, relDiff: { L: dL, F: dF }, sourceChoice, anchorChoice, winnerCellOverall: eligible[0], p7: eligible[0] === "bayer8/world-octave" || eligible[0] === "blue64/world-octave" ? (eligible[0] === "bayer8/world-octave" ? "hit" : "anchor hit, source miss") : "miss" };
console.log(JSON.stringify(decision, null, 1));

// ---- stills for the one-rater look: random names, key hash fixed first ----
const out = opt("--out", null), stillsDir = opt("--stills", null);
if (stillsDir) {
  mkdirSync(stillsDir, { recursive: true });
  const names = [], order = Object.keys(stills).flatMap((k) => [0, 1].map((f) => [k, f]));
  let seed = 20261015; const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
  for (let i = order.length - 1; i > 0; i--) { const j = Math.floor(rnd() * (i + 1)); [order[i], order[j]] = [order[j], order[i]]; }
  const map = {};
  order.forEach(([k, f], i) => {
    const id = `s${String(i + 1).padStart(2, "0")}`, up = new Uint8ClampedArray(W * 3 * H * 3 * 4), src = stills[k][f];
    for (let y = 0; y < H * 3; y++) for (let x = 0; x < W * 3; x++) { const s = (Math.floor(y / 3) * W + Math.floor(x / 3)) * 4, d = (y * W * 3 + x) * 4; up[d] = src[s]; up[d + 1] = src[s + 1]; up[d + 2] = src[s + 2]; up[d + 3] = 255; }
    writePng(`${stillsDir}/${id}.png`, { width: W * 3, height: H * 3, data: up }); map[id] = { cell: k, frame: 10 + f }; names.push(id);
  });
  const keyText = JSON.stringify(map, null, 1);
  writeFileSync(`${stillsDir}/key.json`, keyText);
  decision.stills_key_sha256 = createHash("sha256").update(keyText).digest("hex");
  console.log("stills key sha256", decision.stills_key_sha256);
}
if (out) writeFileSync(out, JSON.stringify({ protocol: "evidence/ditherlight-motion-protocol.json", size: [W, H], frames: NF, results, checks, decision }, null, 1) + "\n");
