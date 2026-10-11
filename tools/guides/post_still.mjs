// Template looks for a guide still, from the shader library's CPU references:
//   node tools/guides/post_still.mjs in.png out.png paint [--scale k]
//   node tools/guides/post_still.mjs in.png out.png film [--ev e]
//   node tools/guides/post_still.mjs in.png out.png upscale [--scale k]
//   any look: [--hud-black b.png --hud-white w.png] lays a separately rendered hud on top.
// The frame is decoded from sRGB to linear light first, which is what both models take in.
// paint: createPaint("oil-grotesque"), the model the GPU pass in PR #55 is checked against.
//   --scale paints a k-times smaller copy and scales it back by nearest neighbour, which keeps
//   the CPU time down and makes the strokes k times larger.
// film: createFilm("500t-print") then crt-classic (the Studio's tube, retro-crt.js) with the
//   retro-thriller template's own settings. The Motion film pass reads its input as scene-linear,
//   and the guides' colours are sRGB, so the film runs here on the decoded frame instead.
import { readFileSync } from "node:fs";
import { inflateSync } from "node:zlib";
import { createPaint, encodePaint8 } from "../../web/shaders/paint/paint.mjs";
import { writePng } from "../shaders/png.mjs";
import { createFilm, encodeFilm8 } from "../../web/shaders/film/run.mjs";
import { classicRef } from "../../web/shaders/crt-classic/classic.mjs";

// A PNG reader for 8-bit RGB or RGBA, non-interlaced: enough for ffmpeg's and our own output.
export function readPng(path) {
  const b = readFileSync(path);
  let p = 8, W = 0, H = 0, ch = 4;
  const idat = [];
  while (p < b.length) {
    const n = b.readUInt32BE(p), type = b.toString("ascii", p + 4, p + 8), d = b.subarray(p + 8, p + 8 + n);
    if (type === "IHDR") {
      W = d.readUInt32BE(0); H = d.readUInt32BE(4);
      if (d[8] !== 8 || d[12] !== 0 || (d[9] !== 6 && d[9] !== 2)) throw new Error(`${path}: only 8-bit RGB(A), non-interlaced`);
      ch = d[9] === 6 ? 4 : 3;
    } else if (type === "IDAT") idat.push(d);
    p += 12 + n;
  }
  const raw = inflateSync(Buffer.concat(idat)), stride = W * ch, out = new Uint8ClampedArray(W * H * 4), prev = new Uint8Array(stride), cur = new Uint8Array(stride);
  for (let y = 0; y < H; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {
      const a = i >= ch ? cur[i - ch] : 0, up = prev[i], c = i >= ch ? prev[i - ch] : 0;
      const pr = f === 4 ? (() => { const q = a + up - c, pa = Math.abs(q - a), pb = Math.abs(q - up), pc = Math.abs(q - c); return pa <= pb && pa <= pc ? a : pb <= pc ? up : c; })() : 0;
      cur[i] = (row[i] + [0, a, up, (a + up) >> 1, pr][f]) & 255;
    }
    for (let x = 0; x < W; x++) { out.set(cur.subarray(x * ch, x * ch + 3), (y * W + x) * 4); out[(y * W + x) * 4 + 3] = ch === 4 ? cur[x * ch + 3] : 255; }
    prev.set(cur);
  }
  return { width: W, height: H, data: out };
}

const toLin = (v) => { const c = v / 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };

// Two measured faults of the paint reference on 11 October 2026 are worked around here, and
// recorded in the guides' PROGRESS.md: flat near-black (sRGB 3, 3, 3) decodes to magenta
// (255, 0, 255), and saturated orange (240, 107, 31) comes out as (150, 135, 74). So the input
// is floored at FLOOR in linear light, the painted luminance is stretched back down from the
// floor's painted value, and the chroma (BT.709 Cb, Cr) comes from the unpainted frame.
const FLOOR = 0.035, Y_FLOOR = 0.15;
const ycc = (r, g, b) => { const y = 0.2126 * r + 0.7152 * g + 0.0722 * b; return [y, (b - y) / 1.8556, (r - y) / 1.5748]; };
const rgb = (y, cb, cr) => { const r = y + 1.5748 * cr, b = y + 1.8556 * cb; return [r, (y - 0.2126 * r - 0.0722 * b) / 0.7152, b]; };

export function paintImage(img, preset = "oil-grotesque", k = 1) {
  const w = Math.floor(img.width / k), h = Math.floor(img.height / k), d = new Float32Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 3; c++) {
    let s = 0;
    for (let j = 0; j < k; j++) for (let i = 0; i < k; i++) s += toLin(img.data[((y * k + j) * img.width + x * k + i) * 4 + c]);
    d[(y * w + x) * 4 + c] = Math.max(FLOOR, s / (k * k));
  }
  for (let i = 3; i < d.length; i += 4) d[i] = 1;
  const r = createPaint(preset, {}, { w, h }).frame({ width: w, height: h, data: d });
  const small = encodePaint8(r.img, w, h), W = w * k, H = h * k, o = new Uint8ClampedArray(W * H * 4);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const si = ((((y / k) | 0) * w) + ((x / k) | 0)) * 4, oi = (y * W + x) * 4;
    const [yp] = ycc(small[si] / 255, small[si + 1] / 255, small[si + 2] / 255);
    const [, cb, cr] = ycc(img.data[oi] / 255, img.data[oi + 1] / 255, img.data[oi + 2] / 255);
    const out = rgb(Math.max(0, (yp - Y_FLOOR) / (1 - Y_FLOOR)), cb, cr);
    for (let c = 0; c < 3; c++) o[oi + c] = Math.round(Math.min(1, Math.max(0, out[c])) * 255);
    o[oi + 3] = 255;
  }
  return { width: W, height: H, data: o };
}

// Pixel mode by hand: a frame rendered at the low resolution, scaled up by nearest neighbour
// (the "integer" mode of web/motion/pixel_mode.mjs).
export function upscale(img, k) {
  const W = img.width * k, H = img.height * k, o = new Uint8ClampedArray(W * H * 4);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) o.set(img.data.subarray((((y / k) | 0) * img.width + ((x / k) | 0)) * 4, (((y / k) | 0) * img.width + ((x / k) | 0)) * 4 + 4), (y * W + x) * 4);
  return { width: W, height: H, data: o };
}

// Lay a hud over finished art. The hud was rendered alone on black (B) and on white (Wh), so
// its coverage is a = 1 - (Wh - B) / 255 and its colour B / a, exactly, per channel.
export function overlayHud(art, B, Wh) {
  const o = new Uint8ClampedArray(art.data);
  for (let i = 0; i < o.length; i += 4) for (let c = 0; c < 3; c++) {
    const a = 1 - (Wh.data[i + c] - B.data[i + c]) / 255;
    if (a > 0.002) o[i + c] = Math.round(B.data[i + c] + (1 - a) * art.data[i + c]);
  }
  return { ...art, data: o };
}

// Film then the soft tube, as the retro-thriller template stacks them.
export const TUBE = { cell: 3, scanlines: true, scanStrength: 0.25, mask: "grille", maskStrength: 0.15, bloom: 0.1, curvature: 0.06, aberration: 0.08, vignette: 0.35 };
export function filmImage(img, ev = 0, tube = TUBE) {
  const { width: w, height: h } = img, d = new Float32Array(w * h * 4);
  for (let i = 0; i < w * h; i++) { for (let c = 0; c < 3; c++) d[i * 4 + c] = toLin(img.data[i * 4 + c]); d[i * 4 + 3] = 1; }
  const r = createFilm("500t-print", { ev }, { w, h }, { w, h }).frame({ width: w, height: h, data: d }, 0);
  const bytes = encodeFilm8(r.img, w, h);
  return { width: w, height: h, data: tube ? classicRef(bytes, w, h, tube) : bytes };
}

const args = process.argv.slice(2), opt = (k, dflt) => { const i = args.indexOf(k); return i >= 0 ? Number(args[i + 1]) : dflt; };
const [inp, out, look = "paint"] = args.filter((a, i) => !a.startsWith("--") && !(args[i - 1] || "").startsWith("--"));
if (inp && out) {
  const t0 = Date.now(), img = readPng(inp);
  const res = look === "film" ? filmImage(img, opt("--ev", 0), args.includes("--no-tube") ? null : TUBE)
    : look === "upscale" ? upscale(img, opt("--scale", 4)) : paintImage(img, "oil-grotesque", opt("--scale", 1));
  const hb = args.indexOf("--hud-black"), hw = args.indexOf("--hud-white");
  writePng(out, hb >= 0 && hw >= 0 ? overlayHud(res, readPng(args[hb + 1]), readPng(args[hw + 1])) : res);
  console.log(`${out}: ${look}, ${((Date.now() - t0) / 1000).toFixed(1)} s`);
}
