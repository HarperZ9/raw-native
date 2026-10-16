// Renders the sodium demo: Sodium Quay at night in rain. The camera walks down the right pavement
// along the painted tile strip, from under a low-pressure sodium lamp (every tile the same orange)
// toward a lit shop (the colours come back), while a van with white LED headlights drives past.
// Also writes a comparison still: the same frame lit the RGB way (a sodium-coloured RGB light times
// an RGB albedo), which keeps the colours that real sodium light removes. CPU only, worker pool.
//   node tools/shaders/sodium_demo.mjs --out DIR [--frames 120] [--w 640 --h 360]
import { Worker, isMainThread, parentPort } from "node:worker_threads";
import { mkdirSync } from "node:fs";
import os from "node:os";
import { renderSodiumGbuffer } from "../../web/shaders/fixtures/sodium-quay-render.mjs";
import { resolveSodium, runSodium } from "../../web/shaders/sodium/shade.mjs";
import { LIGHT_TYPES } from "../../web/shaders/sodium/spectra.mjs";
import { labEncode8 } from "../../web/shaders/lab/gpu-kit.mjs";
import { writePng } from "./png.mjs";

export const DEMO = { ambient: 0.006, rain: 0.5 }; // a darker sky than the study's default 0.03 (stated)
export const EXPOSURE = 1.4;
const camAt = (u) => ({ pos: [3.5 + 0.4 * u, 1.45 + 0.03 * Math.sin(u * 9), -9 - 4.5 * u], look: [5.2 + 0.2 * u, 0.15, -21 - 2 * u] });
const tAt = (u) => 2.6 + 3.8 * u;

// The RGB way: a material's RGB albedo (its colour under the sky light, divided channel by channel
// by the sky light's own colour) times the sodium light's RGB colour. Everything else unchanged.
function rgbWayPlan(plan) {
  const t = plan.table, s = LIGHT_TYPES.indexOf("sodium"), k = LIGHT_TYPES.indexOf("sky"), ws = t.white[s], wk = t.white[k];
  const rgb = t.rgb.map((row, L) => (L !== s ? row : row.map((_, m) => [0, 1, 2].map((q) => ws[q] * (t.rgb[k][m][q] / wk[q])))));
  return { ...plan, table: { ...t, rgb } };
}
function renderOne({ u, w, h, path, rgbWay }) {
  const t = tAt(u), g = renderSodiumGbuffer(t, w, h, { cam: camAt(u) }), plan = resolveSodium(DEMO);
  writePng(path, { width: w, height: h, data: labEncode8(runSodium(rgbWay ? rgbWayPlan(plan) : plan, g, t), EXPOSURE) });
}
if (!isMainThread) parentPort.on("message", (job) => { renderOne(job); parentPort.postMessage(job.path); });
else {
  const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
  const out = opt("--out", null), NF = +opt("--frames", 120), W = +opt("--w", 640), H = +opt("--h", 360);
  if (!out) throw new Error("--out DIR");
  mkdirSync(out, { recursive: true });
  const jobs = Array.from({ length: NF }, (_, i) => ({ u: i / (NF - 1), w: W, h: H, path: `${out}/f${String(i).padStart(4, "0")}.png` }));
  jobs.push({ u: 0.15, w: W, h: H, path: `${out}/compare-spectral.png` }, { u: 0.15, w: W, h: H, path: `${out}/compare-rgb-way.png`, rgbWay: true });
  const t0 = Date.now(); let next = 0;
  await Promise.all(Array.from({ length: Math.max(1, Math.min(os.cpus().length - 2, 18)) }, () => new Promise((resolve, reject) => {
    const wk = new Worker(new URL(import.meta.url));
    const feed = () => { if (next >= jobs.length) { wk.terminate(); resolve(); return; } wk.postMessage(jobs[next++]); };
    wk.on("message", feed); wk.on("error", reject); feed();
  })));
  console.log(`${jobs.length} images in ${((Date.now() - t0) / 1000).toFixed(0)} s to ${out}`);
}
