// Lay a separately rendered hud (labels, key terms) over a finished painted still:
//   node tools/guides/post_still.mjs art.png out.png --hud-black b.png --hud-white w.png
// The paint pass is a scene pass, so type drawn in the same list would be painted with the
// art. The hud is rendered alone on black and on white instead, and its exact coverage and
// colour are recovered from the pair. Every look itself (paint, film, tube, dither, pixel
// mode) is the engine's own post stack, applied in the render.
import { readFileSync } from "node:fs";
import { inflateSync } from "node:zlib";
import { writePng } from "../shaders/png.mjs";

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

// The hud was rendered alone on black (B) and on white (Wh), so its coverage is
// a = 1 - (Wh - B) / 255 and its colour B / a, exactly, per channel.
export function overlayHud(art, B, Wh) {
  const o = new Uint8ClampedArray(art.data);
  for (let i = 0; i < o.length; i += 4) for (let c = 0; c < 3; c++) {
    const a = 1 - (Wh.data[i + c] - B.data[i + c]) / 255;
    if (a > 0.002) o[i + c] = Math.round(B.data[i + c] + (1 - a) * art.data[i + c]);
  }
  return { ...art, data: o };
}

const args = process.argv.slice(2), val = (k) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : null; };
const [inp, out] = args.filter((a, i) => !a.startsWith("--") && !(args[i - 1] || "").startsWith("--"));
if (inp && out) {
  const b = val("--hud-black"), w = val("--hud-white");
  if (!b || !w) throw new Error("post_still: --hud-black and --hud-white are required");
  writePng(out, overlayHud(readPng(inp), readPng(b), readPng(w)));
  console.log(`${out}: hud laid over the painted art`);
}
