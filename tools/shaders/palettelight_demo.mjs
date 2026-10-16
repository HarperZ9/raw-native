// Renders the palette-locked lighting demo: Chapel Yard through a moon that crosses the sky (the
// shadows swing from long to short and back), a flickering lamp and windows, and void pools whose
// palette cycles. Every pixel is one of the 44 palette entries. CPU only; PNG frames at 3x.
//   node tools/shaders/palettelight_demo.mjs --out DIR [--frames 240]
import { mkdirSync } from "node:fs";
import { chapelCamera } from "../../web/shaders/fixtures/chapel-render.mjs";
import { resolvePalettelight, runPalettelight } from "../../web/shaders/palettelight/palettelight.mjs";
import { renderChapels } from "./palettelight_pool.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const out = opt("--out", null), NF = +opt("--frames", 240), W = 320, H = 180, S = 3, FPS = 24;
if (!out) throw new Error("--out DIR");
mkdirSync(out, { recursive: true });
const moonAt = (u) => { const az = ((-100 + 85 * u) * Math.PI) / 180, el = ((14 + 42 * Math.sin(Math.PI * u)) * Math.PI) / 180; return [Math.sin(az) * Math.cos(el), Math.sin(el), Math.cos(az) * Math.cos(el)]; };
const cam = chapelCamera(), t0 = Date.now(), tasks = Array.from({ length: NF }, (_, i) => ({ cam, w: W, h: H, moon: moonAt(i / (NF - 1)) }));
const gs = await renderChapels(tasks);
console.log(`rendered ${NF} G-buffers in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
for (let i = 0; i < NF; i++) {
  const time = i / FPS, plan = resolvePalettelight({ moonDir: tasks[i].moon });
  plan.lamp.intensity = 6 * (0.9 + 0.06 * Math.sin(time * 9) + 0.04 * Math.sin(time * 23 + 1));
  const r = runPalettelight(plan, gs[i], time), up = new Uint8ClampedArray(W * S * H * S * 4);
  for (let y = 0; y < H * S; y++) for (let x = 0; x < W * S; x++) { const s = (Math.floor(y / S) * W + Math.floor(x / S)) * 4, d = (y * W * S + x) * 4; up[d] = r.rgba[s]; up[d + 1] = r.rgba[s + 1]; up[d + 2] = r.rgba[s + 2]; up[d + 3] = 255; }
  writePng(`${out}/f${String(i).padStart(4, "0")}.png`, { width: W * S, height: H * S, data: up });
}
console.log("frames written to", out);
