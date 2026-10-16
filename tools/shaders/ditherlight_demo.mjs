// Renders the demo of dither-quantised lighting: a walk down "The Salt Road" at night, the
// player's lantern in hand, the lighthouse beam sweeping, the tower turning; 1 bit for the first
// part, then 4 tones. CPU only (software). Writes PNG frames at 2x nearest.
//   node tools/shaders/ditherlight_demo.mjs --out DIR [--frames 288] [--w 480 --h 270]
import { mkdirSync } from "node:fs";
import { lightsOf } from "../../web/shaders/fixtures/dream.mjs";
import { resolveDitherlight, runDitherlight } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { renderFrames } from "./ditherlight_pool.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const DARK = +opt("--dark", 0.05), FOG = +opt("--fog", 30), out = opt("--out", null), NF = +opt("--frames", 288), W = +opt("--w", 480), H = +opt("--h", 270);
if (!out) throw new Error("--out DIR");
mkdirSync(out, { recursive: true });
const camAt = (i) => {
  const u = i / (NF - 1), z = 5 - 19 * u, sway = 0.035 * Math.sin(i * 0.21);
  return { pos: [0.6 * Math.sin(u * 3.1), 1.7 + sway, z], yaw: -0.22 + 0.5 * Math.sin(u * Math.PI * 1.2) + 0.015 * Math.sin(i * 0.13), pitch: 0.03 + 0.2 * Math.pow(Math.max(0, u - 0.55) / 0.45, 1.5) };
};
const specs = Array.from({ length: NF }, (_, i) => ({ cam: camAt(i), camPrev: camAt(Math.max(0, i - 1)), tower: 0.01 * i, towerPrev: 0.01 * Math.max(0, i - 1), beamAngle: 1.9 + 0.012 * i, time: i, lanternPos: null, lanternAttached: true, demo: true }));
const t0 = Date.now(), gs = await renderFrames(specs, W, H);
console.log(`rendered ${NF} G-buffers in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
const cut = Math.floor(NF * 0.7), plans = [resolveDitherlight("ink", { dark: DARK, fogDist: FOG }, { height: H }), resolveDitherlight("tones4", { dark: DARK, fogDist: FOG }, { height: H })];
for (let i = 0; i < NF; i++) {
  const r = runDitherlight(plans[i < cut ? 0 : 1], lightsOf(specs[i]), gs[i]), up = new Uint8ClampedArray(W * 2 * H * 2 * 4);
  for (let y = 0; y < H * 2; y++) for (let x = 0; x < W * 2; x++) { const s = ((y >> 1) * W + (x >> 1)) * 4, d = (y * W * 2 + x) * 4; up[d] = r.rgba[s]; up[d + 1] = r.rgba[s + 1]; up[d + 2] = r.rgba[s + 2]; up[d + 3] = 255; }
  writePng(`${out}/f${String(i).padStart(4, "0")}.png`, { width: W * 2, height: H * 2, data: up });
}
console.log("frames written to", out);
