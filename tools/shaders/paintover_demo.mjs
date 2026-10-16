// Renders the paint-over demo: the painted Lamp Quay seen from a camera swinging off-axis (three
// keyframe projectors blend), then a full day at the primary camera graded by the 8-slot look keys.
// CPU only. Writes PNG frames.
//   node tools/shaders/paintover_demo.mjs --out DIR [--orbit 192] [--day 96]
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
import { mkdirSync, readFileSync } from "node:fs";
import os from "node:os";
import { isoCamera } from "../../web/shaders/paintover/camera.mjs";
import { makeProjector, renderProjected, KEYFRAME } from "../../web/shaders/paintover/projector.mjs";
import { paint } from "../../web/shaders/paintover/painter.mjs";
import { renderGbuf } from "../../web/shaders/fixtures/harbour-render.mjs";
import { IDS } from "../../web/shaders/fixtures/harbour.mjs";
import { demoLook, glowOf, gradeFrame } from "../../web/shaders/paintover/grade.mjs";
import { shadePoint } from "../../web/shaders/paintover/projector.mjs";
import { writePng } from "./png.mjs";

const BW = 1152, BH = 648, SW = 960, SH = 540, TARGET = [1, 1.5, 0];
const camAt = (dy, de, scale) => isoCamera({ yawDeg: 45 + dy, elevDeg: 35.264 + de, target: TARGET, scale });
const look = demoLook(8);

function frameFor(spec, projectors) {
  const cam = camAt(spec.dyaw, spec.delev, spec.scale), g = renderGbuf(cam, SW, SH, { shadeOn: false }), r = renderProjected(projectors, g, cam, { opt: KEYFRAME, fallback: (i) => [0.45, 0.5, 0.52] }), lamp = new Float32Array(SW * SH);
  for (let i = 0; i < SW * SH; i++) if (g.id[i] === IDS.lamp || g.id[i] === IDS.lantern) lamp[i] = 1;
  return { rgb: r.rgb, lamp };
}
if (!isMainThread) {
  const projectors = workerData.projectors.map((p) => makeProjector(p.cam, p.w, p.h, p.rgba, p.depth));
  parentPort.on("message", ({ k, spec, out }) => {
    const f = frameFor(spec, projectors), t = look.at(spec.hour), glow = glowOf(f.lamp, SW, SH);
    writePng(`${out}/f${String(k).padStart(4, "0")}.png`, { width: SW, height: SH, data: gradeFrame(f.rgb, SW, SH, t, glow) });
    parentPort.postMessage({ k });
  });
} else {
  const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
  const out = opt("--out", null), NO = +opt("--orbit", 192), ND = +opt("--day", 96);
  if (!out) throw new Error("--out DIR");
  mkdirSync(out, { recursive: true });
  const runs = JSON.parse(readFileSync(new URL("../../evidence/paintover-keyframes-runs.json", import.meta.url))), old = JSON.parse(readFileSync(new URL("../../evidence/paintover-protocol.json", import.meta.url)));
  // build the three projectors (primary, then hole-filling secondaries)
  const cams = [{ dyaw: 0, delev: 0, seed: old.primary.seed }, ...runs.secondaries.map((s, k) => ({ dyaw: s.dyaw, delev: s.delev, seed: old.offaxis.secondaries.seeds[k] }))], projectors = [];
  for (const c of cams) {
    const cam = camAt(c.dyaw, c.delev, 10.8), g = renderGbuf(cam, BW, BH, { shadeOn: true }), n = BW * BH;
    let rgb = g.shade, mask = null;
    if (projectors.length) {
      rgb = new Float32Array(g.shade); mask = new Uint8Array(n); const P = [0, 0, 0], N = [0, 0, 0], hole = new Uint8Array(n);
      for (let i = 0; i < n; i++) { if (!g.hit[i]) continue; for (let k = 0; k < 3; k++) { P[k] = g.P[i * 3 + k]; N[k] = g.N[i * 3 + k]; } const s = shadePoint(projectors, P, N, cam, KEYFRAME); if (s.rgb) { for (let k = 0; k < 3; k++) rgb[i * 3 + k] = s.rgb[k]; } else hole[i] = 1; }
      for (let y = 0; y < BH; y++) for (let x = 0; x < BW; x++) if (hole[y * BW + x]) for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) { const X = x + dx, Y = y + dy; if (X >= 0 && Y >= 0 && X < BW && Y < BH) mask[Y * BW + X] = 1; }
    }
    const p = paint({ w: BW, h: BH, rgb }, { id: g.id, N: g.N }, { seed: c.seed, mask });
    projectors.push(makeProjector(cam, BW, BH, p.rgba8, g.depth));
    console.log(`projector ${projectors.length} built (yaw ${c.dyaw}, elev ${c.delev})`);
  }
  const specs = [];
  for (let k = 0; k < NO; k++) { const u = k / NO; specs.push({ dyaw: 35 * Math.sin(2 * Math.PI * u), delev: 4 * Math.sin(4 * Math.PI * u), scale: 9 * (1 + 0.04 * Math.sin(2 * Math.PI * u)), hour: 12.5 }); }
  const queue = specs.map((s, k) => ({ k, spec: s, out })); let next = 0;
  const workers = Math.min(os.cpus().length - 2, 18, queue.length), plain = projectors.map((p) => ({ cam: p.cam, w: p.w, h: p.h, rgba: p.rgba, depth: p.depth }));
  await Promise.all(Array.from({ length: workers }, () => new Promise((res, rej) => {
    const wk = new Worker(new URL(import.meta.url), { workerData: { projectors: plain } });
    const feed = () => { if (next >= queue.length) { wk.terminate(); res(); return; } wk.postMessage(queue[next++]); };
    wk.on("message", feed); wk.on("error", rej); feed();
  })));
  console.log(`orbit frames written (${NO})`);
  // the day: fixed camera, hour sweeps 0 to 24 over ND frames
  const f = frameFor({ dyaw: 0, delev: 0, scale: 9 }, projectors), glow = glowOf(f.lamp, SW, SH);
  for (let k = 0; k < ND; k++) { const hour = (24 * k) / ND + 5; writePng(`${out}/f${String(NO + k).padStart(4, "0")}.png`, { width: SW, height: SH, data: gradeFrame(f.rgb, SW, SH, look.at(hour), glow) }); }
  console.log(`day frames written (${ND})`);
}
