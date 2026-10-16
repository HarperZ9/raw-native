// Renders G-buffer frames of the second Salt Road (dream2) on a pool of worker threads.
//   const gbufs = await renderFrames(specs, 256, 144);   // same order as specs
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
import os from "node:os";
import { renderFrame2 as renderFrame } from "../../web/shaders/fixtures/dream2.mjs";

export async function renderFrames(specs, w, h, workers = Math.max(1, Math.min(os.cpus().length - 2, 16, specs.length))) {
  const out = new Array(specs.length);
  let next = 0;
  await Promise.all(Array.from({ length: workers }, () => new Promise((resolve, reject) => {
    const wk = new Worker(new URL(import.meta.url), { workerData: { w, h } });
    const feed = () => { if (next >= specs.length) { wk.terminate(); resolve(); return; } const i = next++; wk.postMessage({ i, spec: specs[i] }); };
    wk.on("message", ({ i, g }) => { out[i] = g; feed(); });
    wk.on("error", reject);
    feed();
  })));
  return out;
}

if (!isMainThread) {
  parentPort.on("message", ({ i, spec }) => {
    const g = renderFrame(spec, workerData.w, workerData.h);
    parentPort.postMessage({ i, g });
  });
}
