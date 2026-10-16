// Renders the lens suite demo: Quay Street at night after rain, a van driving toward the camera
// with its headlights on, lamps along the street, all through the lens suite (anamorphic streaks,
// ghosts, starburst, halation, dispersion) with the night slot's keyed controls. Also writes an
// eight-slot strip: one frame through the lens controls of each time-of-day slot.
// CPU only, a pool of worker threads, PNG frames.
//   node tools/shaders/lens_demo.mjs --out DIR [--frames 144] [--w 640 --h 360]
import { Worker, isMainThread, parentPort } from "node:worker_threads";
import { mkdirSync } from "node:fs";
import os from "node:os";
import { renderQuay } from "../../web/shaders/fixtures/quay-street-render.mjs";
import { resolveLens, extractSources, prepareSources, runLens } from "../../web/shaders/lens/lens.mjs";
import { lensAtHour } from "../../web/shaders/lens/params.mjs";
import { SLOTS_8 } from "../../web/look/keys.mjs";
import { labEncode8 } from "../../web/shaders/lab/gpu-kit.mjs";
import { writePng } from "./png.mjs";

// The demo's lens: the slot-keyed controls plus a fixed body (a 1.33x anamorphic, a coating whose
// quarter wave sits at 600 nm so its residual reflects blue, a shorter streak).
export const DEMO_BODY = { squeeze: 1.33, coating: { quarterAt: 600 }, streakLenPx: 100 };
export const EXPOSURE = 1.2;

function renderOne({ t, hour, w, h, path, raw }) {
  const f = renderQuay(t, w, h), plan = resolveLens({ ...lensAtHour(hour), ...DEMO_BODY }, { width: w, height: h }), src = extractSources(plan, f);
  const out = runLens(plan, f, prepareSources(plan, src.list), src.keep);
  writePng(path, { width: w, height: h, data: labEncode8(out, EXPOSURE) });
  if (raw) writePng(raw, { width: w, height: h, data: labEncode8(f, EXPOSURE) });
  return src.list.length;
}

if (!isMainThread) parentPort.on("message", (job) => parentPort.postMessage({ i: job.i, n: renderOne(job) }));
else {
  const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
  const out = opt("--out", null), NF = +opt("--frames", 144), W = +opt("--w", 640), H = +opt("--h", 360);
  if (!out) throw new Error("--out DIR");
  mkdirSync(out, { recursive: true });
  const jobs = Array.from({ length: NF }, (_, i) => ({ i, t: (6.0 * i) / (NF - 1), hour: 22, w: W, h: H, path: `${out}/f${String(i).padStart(4, "0")}.png`, raw: i === Math.round(NF * 0.7) ? `${out}/raw-${i}.png` : null }));
  SLOTS_8.forEach((s, k) => jobs.push({ i: NF + k, t: 5, hour: s.hour, w: W / 2, h: H / 2, path: `${out}/slot-${k}-${s.name}.png` }));
  const t0 = Date.now(); let next = 0;
  await Promise.all(Array.from({ length: Math.max(1, Math.min(os.cpus().length - 2, 18)) }, () => new Promise((resolve, reject) => {
    const wk = new Worker(new URL(import.meta.url));
    const feed = () => { if (next >= jobs.length) { wk.terminate(); resolve(); return; } wk.postMessage(jobs[next++]); };
    wk.on("message", feed); wk.on("error", reject); feed();
  })));
  console.log(`${jobs.length} images in ${((Date.now() - t0) / 1000).toFixed(0)} s to ${out}`);
}
