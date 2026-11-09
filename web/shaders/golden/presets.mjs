// Fingerprints of every existing film, CRT and lens preset on a small fixed scene (CPU reference).
// library.test.mjs compares them with recorded.mjs, so an existing look can change only on purpose: a deliberate change re-records
// recorded.mjs (node web/shaders/golden/presets.mjs --write prints the block) in the same commit and says why.
import { createHash } from "node:crypto";
import { RECORDED } from "./recorded.mjs";
import { fileURLToPath } from "node:url";
import { createFilm } from "../film/run.mjs";
import { createCrt } from "../crt/crt.mjs";
import { resolveLens, extractSources, prepareSources, runLens } from "../lens/lens.mjs";

// Shared names known when the file was recorded; new presets are fingerprinted too but only these must match.
export const RECORDED_FILM = ["500t-print", "500t-no-remjet", "250d-print", "bleach-bypass"];
export const RECORDED_CRT = ["pvm-20", "pvm-20-soft", "trinitron-tv", "slot-tv", "vga-14", "bw-tv", "radar-p7", "scope-p31"];

const hash = (arr) => {
  const q = Int32Array.from(Array.from(arr, (v) => Math.round(v * 1e5)));
  return createHash("sha256").update(Buffer.from(q.buffer)).digest("hex").slice(0, 16);
};
function scene(W, H) {
  const s = { width: W, height: H, data: new Float32Array(W * H * 4) };
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4, u = x / (W - 1), v = y / (H - 1);
    s.data[i] = 0.02 + 0.5 * u * u; s.data[i + 1] = 0.02 + 0.4 * v; s.data[i + 2] = 0.05 + 0.3 * (1 - u) * v; s.data[i + 3] = 1;
  }
  s.data.set([40, 30, 20, 1], ((H >> 1) * W + (W >> 2)) * 4);
  return s;
}
export function fingerprints(filmNames = RECORDED_FILM, crtNames = RECORDED_CRT) {
  const out = { film: {}, crt: {}, lens: {} };
  const sc = scene(48, 32);
  for (const n of filmNames) out.film[n] = hash(createFilm(n, { weaveUm: 0, jitterUm: 0 }, { w: 48, h: 32 }, { w: 48, h: 32 }).frame(sc, 0).img);
  const sig = { width: 32, height: 24, data: new Float32Array(32 * 24 * 4) };
  for (let i = 0; i < 32 * 24; i++) { sig.data[4 * i] = (i % 32) / 31; sig.data[4 * i + 1] = Math.floor(i / 32) / 23; sig.data[4 * i + 2] = 0.5; sig.data[4 * i + 3] = 1; }
  for (const n of crtNames) out.crt[n] = hash(createCrt(n, {}, { w: 32, h: 24 }, { w: 64, h: 48 }).frame(sig, 0).img);
  const W = 64, H = 48, fr = { width: W, height: H, data: new Float64Array(W * H * 4) };
  fr.data.set(Array.from(sc.data.slice(0, W * H * 4 > sc.data.length ? sc.data.length : W * H * 4)));
  for (let i = 0; i < W * H; i++) fr.data[4 * i + 3] = 1;
  fr.data.set([60, 50, 40, 1], ((H >> 1) * W + (W >> 1)) * 4);
  const plan = resolveLens({}, { width: W, height: H }), src = extractSources(plan, fr);
  out.lens.default = hash(runLens(plan, fr, prepareSources(plan, src.list), src.keep).data);
  return out;
}
export const recorded = () => RECORDED;
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1] && process.argv.includes("--write")) {
  // Prints the block for recorded.mjs; paste it by hand so a re-record is a reviewed diff. The JSON form is git-ignored.
  console.log(JSON.stringify(fingerprints(), null, 1));
}
