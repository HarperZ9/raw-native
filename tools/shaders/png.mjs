// A minimal PNG writer (RGBA8, no filter) and reader (8-bit RGB or RGBA, any filter, not
// interlaced) on node's zlib, so the sheet tools need no package.
import { deflateSync, inflateSync } from "node:zlib";
import { readFileSync, writeFileSync } from "node:fs";

const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
function crc32(buf) { let c = 0xffffffff; for (const b of buf) c = CRC[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length), t = Buffer.from(type, "ascii");
  out.writeUInt32BE(data.length, 0); t.copy(out, 4); Buffer.from(data).copy(out, 8);
  out.writeUInt32BE(crc32(Buffer.concat([t, Buffer.from(data)])), 8 + data.length);
  return out;
}
export function writePng(path, { width, height, data }) {
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) { raw[y * (width * 4 + 1)] = 0; Buffer.from(data.buffer, data.byteOffset + y * width * 4, width * 4).copy(raw, y * (width * 4 + 1) + 1); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 6;
  writeFileSync(path, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0))]));
}
// Read an 8-bit RGB or RGBA PNG that is not interlaced (ours, and ffmpeg's) into RGBA8.
// An RGB file reads with alpha 255. Anything else throws rather than guessing.
export function readPng(pathOrBytes) {
  const b = typeof pathOrBytes === "string" ? readFileSync(pathOrBytes) : Buffer.from(pathOrBytes);
  const name = typeof pathOrBytes === "string" ? pathOrBytes : "png";
  if (b.length < 8 || b.readUInt32BE(0) !== 0x89504e47) throw new Error(`${name}: not a PNG`);
  let p = 8, W = 0, H = 0, ch = 0;
  const idat = [];
  while (p + 8 <= b.length) {
    const n = b.readUInt32BE(p), type = b.toString("ascii", p + 4, p + 8), d = b.subarray(p + 8, p + 8 + n);
    if (type === "IHDR") {
      W = d.readUInt32BE(0); H = d.readUInt32BE(4);
      if (d[8] !== 8 || d[12] !== 0 || (d[9] !== 6 && d[9] !== 2)) throw new Error(`${name}: only 8-bit RGB(A), not interlaced`);
      ch = d[9] === 6 ? 4 : 3;
    } else if (type === "IDAT") idat.push(d);
    else if (type === "IEND") break;
    p += 12 + n;
  }
  if (!ch) throw new Error(`${name}: no IHDR`);
  const raw = inflateSync(Buffer.concat(idat)), stride = W * ch;
  if (raw.length < (stride + 1) * H) throw new Error(`${name}: image data is short`);
  const out = new Uint8ClampedArray(W * H * 4), prev = new Uint8Array(stride), cur = new Uint8Array(stride);
  for (let y = 0; y < H; y++) {
    const f = raw[y * (stride + 1)], row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    if (f > 4) throw new Error(`${name}: row ${y} has filter ${f}`);
    for (let i = 0; i < stride; i++) {
      const a = i >= ch ? cur[i - ch] : 0, up = prev[i], c = i >= ch ? prev[i - ch] : 0;
      let pred = 0;
      if (f === 1) pred = a;
      else if (f === 2) pred = up;
      else if (f === 3) pred = (a + up) >> 1;
      else if (f === 4) { const q = a + up - c, pa = Math.abs(q - a), pb = Math.abs(q - up), pc = Math.abs(q - c); pred = pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      cur[i] = (row[i] + pred) & 255;
    }
    for (let x = 0; x < W; x++) { out.set(cur.subarray(x * ch, x * ch + 3), (y * W + x) * 4); out[(y * W + x) * 4 + 3] = ch === 4 ? cur[x * ch + 3] : 255; }
    prev.set(cur);
  }
  return { width: W, height: H, data: out };
}
// Crop and nearest-scale helpers for sheets.
export function crop(img, x, y, w, h) {
  const o = new Uint8ClampedArray(w * h * 4);
  for (let j = 0; j < h; j++) o.set(img.data.subarray(((y + j) * img.width + x) * 4, ((y + j) * img.width + x + w) * 4), j * w * 4);
  return { width: w, height: h, data: o };
}
export function nearest(img, k) {
  const w = img.width * k, h = img.height * k, o = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) o.set(img.data.subarray(((((y / k) | 0) * img.width) + ((x / k) | 0)) * 4, ((((y / k) | 0) * img.width) + ((x / k) | 0)) * 4 + 4), (y * w + x) * 4);
  return { width: w, height: h, data: o };
}
