// Renders the second dither-lighting demo: a low walk through "The Salt Road" (dream2), lantern in
// hand, under hanging arches and through the ring, with the lighthouse beam sweeping the haze and
// the moon, the spire and the shards in view. 1 bit first, then 4 tones. CPU only.
//   node tools/shaders/ditherlight_demo2.mjs --out DIR [--frames 288] [--w 480 --h 270]
import { mkdirSync } from "node:fs";
import { lightsOf2 } from "../../web/shaders/fixtures/dream2.mjs";
import { resolveDitherlight, runDitherlight } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { renderFrames } from "./ditherlight_pool2.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const out = opt("--out", null), NF = +opt("--frames", 288), W = +opt("--w", 480), H = +opt("--h", 270), STILLS = opt("--stills", null);
if (!out) throw new Error("--out DIR");
mkdirSync(out, { recursive: true });
const ease = (x) => x * x * (3 - 2 * x);
function specAt(i) {
  const u = i / (NF - 1), z = 5 - 26 * u, sway = 0.03 * Math.sin(i * 0.22);
  const yaw = 0.2 - 0.34 * Math.sin(Math.PI * Math.min(1, u * 1.6)) + 0.5 * ease(Math.max(0, (u - 0.62) / 0.38)) + 0.012 * Math.sin(i * 0.13);
  const pitch = 0.06 + 0.05 * Math.sin(u * 6) + 0.2 * ease(Math.max(0, (u - 0.7) / 0.3));
  return { cam: { pos: [0.5 * Math.sin(u * 3.1), 1.1 + sway, z], yaw, pitch }, beamAngle: 2.6 + 0.45 * Math.sin(2 * Math.PI * u * 1.5), flicker: 0.6 * Math.sin(i * 0.9) + 0.4 * Math.sin(i * 2.3 + 1), time: i, spin: i * 0.012 };
}
const specs = Array.from({ length: NF }, (_, i) => specAt(i));
const t0 = Date.now(), gs = await renderFrames(specs, W, H);
console.log(`rendered ${NF} G-buffers in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
const cut = Math.floor(NF * 0.65), ov = { dark: 0.012, fogDist: 60 }, plans = [resolveDitherlight("ink", ov, { height: H }), resolveDitherlight("tones4", ov, { height: H })];
for (let i = 0; i < NF; i++) {
  const r = runDitherlight(plans[i < cut ? 0 : 1], lightsOf2(specs[i]), gs[i]), up = new Uint8ClampedArray(W * 2 * H * 2 * 4);
  for (let y = 0; y < H * 2; y++) for (let x = 0; x < W * 2; x++) { const s = ((y >> 1) * W + (x >> 1)) * 4, d = (y * W * 2 + x) * 4; up[d] = r.rgba[s]; up[d + 1] = r.rgba[s + 1]; up[d + 2] = r.rgba[s + 2]; up[d + 3] = 255; }
  writePng(`${out}/f${String(i).padStart(4, "0")}.png`, { width: W * 2, height: H * 2, data: up });
}
console.log("frames written to", out);
void STILLS;
