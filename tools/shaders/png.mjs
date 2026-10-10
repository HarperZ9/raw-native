// A minimal PNG writer (RGBA8, no filter) on node's zlib, so the sheet tools need no package.
import { deflateSync } from "node:zlib";
import { writeFileSync } from "node:fs";

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
