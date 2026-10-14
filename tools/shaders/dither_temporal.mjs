// Temporal stability of the dither modes over moving sequences (evidence/shaders-dither-temporal-protocol.json).
//   node tools/shaders/dither_temporal.mjs [--frames 60] [--out evidence.json] [--clip DIR palette mode sequence n]
// flicker: 100 x mean |e_t - e_(t-1)|, e = OKLab(box4 output) - OKLab(box4 source), linear-light 4 x 4 box.
// toggles: fraction of pixels whose palette index changes between frames (source: 8-bit changes).
import { writeFileSync, mkdirSync } from "node:fs";
import { ditherRef } from "../../web/shaders/dither/dither.mjs";
import { labPalette, SRGB8_TO_LINEAR, linearToOklab } from "../../web/shaders/reference/retro-palettes.mjs";
import { sceneCard } from "../../web/shaders/fixtures/scene.mjs";
import { gameFrame } from "../../web/shaders/fixtures/game.mjs";
import { streetScene } from "../../web/shaders/fixtures/street.mjs";
import { writePng } from "./png.mjs";
import os from "node:os";
try { os.setPriority(os.constants.priority.PRIORITY_BELOW_NORMAL); } catch (e) { console.error("priority not lowered:", e.message); }

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const NF = +opt("--frames", 60);
const enc = (l) => { l = Math.min(1, Math.max(0, l)); return l <= 0.0031308 ? l * 12.92 : 1.055 * Math.pow(l, 1 / 2.4) - 0.055; };
const shoulder = (v) => Math.min(1, 1 - Math.exp(-Math.max(0, v) * 2.4));
const toBytes = (w, h, f) => { const b = new Uint8Array(w * h * 4); for (let p = 0; p < w * h; p++) { for (let c = 0; c < 3; c++) b[p * 4 + c] = Math.floor(Math.min(1, Math.max(0, f(p, c))) * 255 + 0.5); b[p * 4 + 3] = 255; } return b; };

// Each sequence: { w, h, frame(i) -> RGBA8 bytes }.
export function sequences() {
  let day = null;
  const memo = (seq) => { const m = new Map(), f = seq.frame; seq.frame = (i) => { if (!m.has(i)) m.set(i, f(i)); return m.get(i); }; return seq; };
  return Object.fromEntries(Object.entries({
    pan: { w: 320, h: 180, frame(i) {
      day ??= streetScene(640, 400, 0, { daylight: true });
      const ox = 20 + 1.3333333 * i, oy = 110, D = day.data;
      return toBytes(320, 180, (p, c) => {
        const x = (p % 320) + ox, y = Math.floor(p / 320) + oy, x0 = Math.floor(x), fx = x - x0;
        const a = D[(y * 640 + x0) * 4 + c], b = D[(y * 640 + x0 + 1) * 4 + c];
        return enc(shoulder(a + (b - a) * fx));
      });
    } },
    game: { w: 320, h: 240, frame(i) { const g = gameFrame(320, 240, i / 30), d = g.data ?? g; return toBytes(320, 240, (p, c) => d[p * 4 + c]); } },
    lamp: { w: 320, h: 200, frame(i) { const s = streetScene(320, 200, i / 30); return toBytes(320, 200, (p, c) => enc(shoulder(s.data[p * 4 + c]))); } },
    fade: { w: 320, h: 180, frame(i) { const s = sceneCard(320, 180, 0), d = s.data ?? s, k = 0.7 + (0.3 * i) / (NF - 1); return toBytes(320, 180, (p, c) => enc(d[p * 4 + c] * k)); } },
  }).map(([k, v]) => [k, memo(v)]));
}

function lowLab(bytes, w, h) {
  const lin = new Float64Array(w * h * 3), o = new Float64Array(w * h * 3);
  for (let p = 0; p < w * h; p++) for (let c = 0; c < 3; c++) lin[p * 3 + c] = SRGB8_TO_LINEAR[bytes[p * 4 + c]];
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const s = [0, 0, 0];
    for (let dy = -2; dy < 2; dy++) for (let dx = -2; dx < 2; dx++) { const q = (Math.min(h - 1, Math.max(0, y + dy)) * w + Math.min(w - 1, Math.max(0, x + dx))) * 3; s[0] += lin[q]; s[1] += lin[q + 1]; s[2] += lin[q + 2]; }
    const L = linearToOklab(s[0] / 16, s[1] / 16, s[2] / 16), p = (y * w + x) * 3; o[p] = L[0]; o[p + 1] = L[1]; o[p + 2] = L[2];
  }
  return o;
}
const paint = (idx, pal, n) => { const o = new Uint8Array(n * 4); for (let p = 0; p < n; p++) { o.set(pal[idx[p]].rgb, p * 4); o[p * 4 + 3] = 255; } return o; };

export function measure(seq, palette, mode, n = NF) {
  const { w, h } = seq, pal = labPalette(palette), N = w * h;
  let flick = 0, err = 0, tog = 0, stog = 0, prevE = null, prevI = null, prevS = null;
  for (let i = 0; i < n; i++) {
    const src = seq.frame(i), idx = ditherRef(src, w, h, { palette, mode }), out = paint(idx, pal, N);
    const a = lowLab(src, w, h), b = lowLab(out, w, h), e = new Float64Array(N * 3);
    for (let k = 0; k < N * 3; k++) e[k] = b[k] - a[k];
    for (let p = 0; p < N; p++) err += Math.hypot(e[p * 3], e[p * 3 + 1], e[p * 3 + 2]);
    if (prevE) {
      for (let p = 0; p < N; p++) flick += Math.hypot(e[p * 3] - prevE[p * 3], e[p * 3 + 1] - prevE[p * 3 + 1], e[p * 3 + 2] - prevE[p * 3 + 2]);
      for (let p = 0; p < N; p++) { tog += idx[p] !== prevI[p]; stog += src[p * 4] !== prevS[p * 4] || src[p * 4 + 1] !== prevS[p * 4 + 1] || src[p * 4 + 2] !== prevS[p * 4 + 2]; }
    }
    prevE = e; prevI = idx; prevS = src;
  }
  const pairs = (n - 1) * N;
  return { flicker: (100 * flick) / pairs, toggles: tog / pairs, source_changes: stog / pairs, error: (100 * err) / (n * N) };
}

const clipAt = args.indexOf("--clip");
if (clipAt >= 0) {
  // Writes a clip's dithered frames as PNGs: --clip DIR palette mode sequence n
  const [dir, palette, mode, name, n] = args.slice(clipAt + 1), seq = sequences()[name], pal = labPalette(palette);
  mkdirSync(dir, { recursive: true });
  for (let i = 0; i < +n; i++) { const s = seq.frame(i), o = paint(ditherRef(s, seq.w, seq.h, { palette, mode }), pal, seq.w * seq.h); writePng(`${dir}/f${String(i).padStart(2, "0")}.png`, { width: seq.w, height: seq.h, data: o }); }
} else if ((process.argv[1] || "").endsWith("dither_temporal.mjs")) {
  const seqs = sequences(), res = [];
  for (const name of Object.keys(seqs)) for (const palette of ["pico8", "ega"]) for (const mode of ["none", "bayer4", "bayer8", "blue", "mix-bayer4", "mix-bayer8", "mix-blue"]) {
    const t0 = Date.now(), r = { sequence: name, palette, mode, ...measure(seqs[name], palette, mode) };
    res.push(r);
    console.log(`${name}\t${palette}\t${mode}\tflicker ${r.flicker.toFixed(3)}\ttoggles ${r.toggles.toFixed(4)} (source ${r.source_changes.toFixed(4)})\terror ${r.error.toFixed(2)}\t${((Date.now() - t0) / 1000).toFixed(0)} s`);
  }
  const out = opt("--out", null);
  if (out) writeFileSync(out, JSON.stringify({ protocol: "evidence/shaders-dither-temporal-protocol.json", frames: NF, results: res }, null, 1) + "\n");
}
