// Low-pass error of the dither modes, held to evidence/shaders-dither-mix-bounds.json ("quality").
//   node tools/shaders/dither_quality.mjs [--panel D:/shader-work/panel] [--out evidence.json]
// Output and source are each box-filtered 4 x 4 in linear light (stride 1, clamped edges); the
// score is the mean OKLab distance x 100. Exit 1 unless mix-bayer4 is strictly lower than bayer4
// and the per-channel Bayer baseline on every frame and palette (and than the competitor's own
// renders on the panel frames, when --panel is given).
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { inflateSync } from "node:zlib";
import { ditherRef } from "../../web/shaders/dither/dither.mjs";
import { labPalette, SRGB8_TO_LINEAR, linearToOklab } from "../../web/shaders/reference/retro-palettes.mjs";
import { BAYER } from "../../web/shaders/reference/retro-dither.mjs";
import { sceneCard } from "../../web/shaders/fixtures/scene.mjs";
import { gameFrame } from "../../web/shaders/fixtures/game.mjs";
import { testCard } from "../../web/shaders/fixtures/testcard.mjs";
import { gradientPlate } from "../../web/shaders/fixtures/plates.mjs";
import { streetScene } from "../../web/shaders/fixtures/street.mjs";

const args = process.argv.slice(2), opt = (k) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : null; };
const enc = (l) => { l = Math.min(1, Math.max(0, l)); return l <= 0.0031308 ? l * 12.92 : 1.055 * Math.pow(l, 1 / 2.4) - 0.055; };
const bytesOf = (img, signal) => { const b = new Uint8Array(img.width * img.height * 4); for (let i = 0; i < b.length; i++) b[i] = (i & 3) === 3 ? 255 : Math.floor((signal ? Math.min(1, Math.max(0, img.data[i])) : enc(img.data[i])) * 255 + 0.5); return { width: img.width, height: img.height, data: b }; };
const wrap = (d, w, h) => ({ width: w, height: h, data: d });
const street = streetScene(480, 300, 0);
export const FRAMES = {
  "scene-320x180": bytesOf(wrap(sceneCard(320, 180, 0).data ?? sceneCard(320, 180, 0), 320, 180), false),
  "game-320x240": bytesOf(wrap(gameFrame(320, 240, 0.5).data ?? gameFrame(320, 240, 0.5), 320, 240), true),
  "testcard-256x224": bytesOf(wrap(testCard(256, 224).data ?? testCard(256, 224), 256, 224), true),
  "gradient-320x180": gradientPlate(320, 180),
  "street-480x300": bytesOf(wrap(street.data, 480, 300), false),
};
// The per-channel Bayer 4 x 4 baseline: each sRGB channel offset by (t - 0.5) / 3, then the
// nearest palette colour in sRGB.
export function perChannel(bytes, w, h, pal) {
  const out = new Uint16Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const p = y * w + x, t = (BAYER[4][y % 4][x % 4] + 0.5) / 16, o = (t - 0.5) / 3, v = [0, 1, 2].map((c) => bytes[p * 4 + c] / 255 + o);
    let best = 0, bd = Infinity;
    pal.forEach((e, k) => { const d = e.rgb.reduce((a, q, c) => a + (q / 255 - v[c]) ** 2, 0); if (d < bd) { bd = d; best = k; } });
    out[p] = best;
  }
  return out;
}
function boxLin(lin, w, h) {
  const o = new Float64Array(w * h * 3);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 3; c++) {
    let s = 0;
    for (let dy = -2; dy < 2; dy++) for (let dx = -2; dx < 2; dx++) { const xx = Math.min(w - 1, Math.max(0, x + dx)), yy = Math.min(h - 1, Math.max(0, y + dy)); s += lin[(yy * w + xx) * 3 + c]; }
    o[(y * w + x) * 3 + c] = s / 16;
  }
  return o;
}
const linOfBytes = (b, w, h) => { const o = new Float64Array(w * h * 3); for (let p = 0; p < w * h; p++) for (let c = 0; c < 3; c++) o[p * 3 + c] = SRGB8_TO_LINEAR[b[p * 4 + c]]; return o; };
export function lowPassError(srcBytes, outBytes, w, h) {
  const a = boxLin(linOfBytes(srcBytes, w, h), w, h), b = boxLin(linOfBytes(outBytes, w, h), w, h);
  let s = 0;
  for (let p = 0; p < w * h; p++) { const A = linearToOklab(a[p * 3], a[p * 3 + 1], a[p * 3 + 2]), B = linearToOklab(b[p * 3], b[p * 3 + 1], b[p * 3 + 2]); s += Math.hypot(A[0] - B[0], A[1] - B[1], A[2] - B[2]); }
  return (100 * s) / (w * h);
}
const paint = (idx, pal, w, h) => { const o = new Uint8Array(w * h * 4); for (let p = 0; p < w * h; p++) { o.set(pal[idx[p]].rgb, p * 4); o[p * 4 + 3] = 255; } return o; };
function readPngRGB(path) {
  const b = readFileSync(path); let p = 8, W = 0, H = 0, ch = 3; const idat = [];
  while (p < b.length) { const n = b.readUInt32BE(p), t = b.toString("ascii", p + 4, p + 8), d = b.subarray(p + 8, p + 8 + n); if (t === "IHDR") { W = d.readUInt32BE(0); H = d.readUInt32BE(4); ch = d[9] === 6 ? 4 : 3; } else if (t === "IDAT") idat.push(d); p += 12 + n; }
  const raw = inflateSync(Buffer.concat(idat)), st = W * ch, out = new Uint8Array(W * H * 4), prev = new Uint8Array(st), cur = new Uint8Array(st);
  for (let y = 0; y < H; y++) {
    const f = raw[y * (st + 1)], row = raw.subarray(y * (st + 1) + 1, (y + 1) * (st + 1));
    for (let i = 0; i < st; i++) { const a = i >= ch ? cur[i - ch] : 0, u = prev[i], c = i >= ch ? prev[i - ch] : 0, q = a + u - c, pa = Math.abs(q - a), pb = Math.abs(q - u), pc = Math.abs(q - c); cur[i] = (row[i] + [0, a, u, (a + u) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? u : c][f]) & 255; }
    for (let x = 0; x < W; x++) { out.set(cur.subarray(x * ch, x * ch + 3), (y * W + x) * 4); out[(y * W + x) * 4 + 3] = 255; }
    prev.set(cur);
  }
  return { width: W, height: H, data: out };
}

const res = { bounds: "evidence/shaders-dither-mix-bounds.json", scores: {}, comparisons: [] };
for (const [name, f] of Object.entries(FRAMES)) for (const palette of ["ega", "pico8"]) {
  const pal = labPalette(palette), { width: w, height: h, data } = f, sc = {};
  for (const mode of ["bayer4", "mix-bayer4", "mix-blue", "blue"]) sc[mode] = lowPassError(data, paint(ditherRef(data, w, h, { palette, mode }), pal, w, h), w, h);
  sc["per-channel-bayer4"] = lowPassError(data, paint(perChannel(data, w, h, pal), pal, w, h), w, h);
  const panelDir = opt("--panel");
  if (panelDir && palette === "ega" && (name === "scene-320x180" || name === "game-320x240")) {
    const inRaw = new Uint8Array(readFileSync(`${panelDir}/in/${name}.rgba`)).subarray(8);   // 8-byte header: width, height
    res.inputsMatchPanel = (res.inputsMatchPanel ?? true) && inRaw.length === data.length && inRaw.every((v, i) => v === data[i]);
    for (const comp of ["bayer4-ega", "bluenoise-ega"]) { const pth = `${panelDir}/out/${comp}-${name}-${name.split("x")[0].split("-")[1]}.png`; if (existsSync(pth)) sc["competitor " + comp] = lowPassError(data, readPngRGB(pth).data, w, h); }
  }
  for (const k of Object.keys(sc)) sc[k] = +sc[k].toFixed(3);
  res.scores[`${name} ${palette}`] = sc;
  for (const k of ["bayer4", "per-channel-bayer4", "competitor bayer4-ega"]) if (k in sc) res.comparisons.push({ frame: name, palette, against: k, mix: sc["mix-bayer4"], other: sc[k], pass: sc["mix-bayer4"] < sc[k] });
}
res.pass = res.comparisons.every((c) => c.pass);
for (const [k, v] of Object.entries(res.scores)) console.log(k.padEnd(24), JSON.stringify(v));
console.log(res.comparisons.filter((c) => !c.pass).length, "of", res.comparisons.length, "comparisons fail; panel inputs match:", res.inputsMatchPanel);
if (opt("--out")) writeFileSync(opt("--out"), JSON.stringify(res, null, 1) + "\n");
process.exit(res.pass ? 0 : 1);
