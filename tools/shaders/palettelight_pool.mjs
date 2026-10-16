// Renders Chapel Yard G-buffers on a pool of worker threads.
//   const gs = await renderChapels([{ cam, w, h, lamp }, ...]);   // same order
import { Worker, isMainThread, parentPort } from "node:worker_threads";
import os from "node:os";
import { renderChapel } from "../../web/shaders/fixtures/chapel-render.mjs";

export async function renderChapels(tasks, workers = Math.max(1, Math.min(os.cpus().length - 2, 18, tasks.length))) {
  const out = new Array(tasks.length);
  let next = 0;
  await Promise.all(Array.from({ length: workers }, () => new Promise((resolve, reject) => {
    const wk = new Worker(new URL(import.meta.url));
    const feed = () => { if (next >= tasks.length) { wk.terminate(); resolve(); return; } const i = next++; wk.postMessage({ i, t: tasks[i] }); };
    wk.on("message", ({ i, g }) => { out[i] = g; feed(); });
    wk.on("error", reject);
    feed();
  })));
  return out;
}
if (!isMainThread) parentPort.on("message", ({ i, t }) => parentPort.postMessage({ i, g: renderChapel(t.cam, t.w, t.h, { lamp: t.lamp, moon: t.moon }) }));
