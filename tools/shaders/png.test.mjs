// readPng against writePng, and against a PNG built here with every row filter (RGB and RGBA),
// so the reader is checked on files it did not write itself.
import test from "node:test";
import assert from "node:assert/strict";
import { deflateSync } from "node:zlib";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { readPng, writePng } from "./png.mjs";

const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc32 = (buf) => { let c = 0xffffffff; for (const v of buf) c = CRC[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function chunk(type, data) {
  const t = Buffer.from(type, "ascii"), out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0); t.copy(out, 4); data.copy(out, 8);
  out.writeUInt32BE(crc32(Buffer.concat([t, data])), 8 + data.length);
  return out;
}
const paeth = (a, b, c) => { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; };
// Encode pixels (ch channels per pixel) with row y using filter y % 5.
function encodeFiltered(W, H, ch, px) {
  const stride = W * ch, raw = Buffer.alloc((stride + 1) * H);
  for (let y = 0; y < H; y++) {
    const f = y % 5; raw[y * (stride + 1)] = f;
    for (let i = 0; i < stride; i++) {
      const x = px[y * stride + i], a = i >= ch ? px[y * stride + i - ch] : 0, b = y ? px[(y - 1) * stride + i] : 0, c = y && i >= ch ? px[(y - 1) * stride + i - ch] : 0;
      const pred = [0, a, b, (a + b) >> 1, paeth(a, b, c)][f];
      raw[y * (stride + 1) + 1 + i] = (x - pred) & 255;
    }
  }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = ch === 4 ? 6 : 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}
const pattern = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 37 + seed * 11 + ((i * i) >> 3)) & 255);

test("readPng returns what writePng wrote, byte for byte", () => {
  const dir = mkdtempSync(join(tmpdir(), "png-")), W = 7, H = 5, data = new Uint8ClampedArray(pattern(W * H * 4, 1));
  try {
    writePng(join(dir, "a.png"), { width: W, height: H, data });
    const r = readPng(join(dir, "a.png"));
    assert.equal(r.width, W); assert.equal(r.height, H);
    assert.deepEqual([...r.data], [...data]);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

for (const ch of [3, 4]) test(`readPng undoes all five row filters (${ch === 4 ? "RGBA" : "RGB"})`, () => {
  const W = 6, H = 10, px = pattern(W * H * ch, ch), r = readPng(encodeFiltered(W, H, ch, px));
  for (let p = 0; p < W * H; p++) for (let k = 0; k < 4; k++) assert.equal(r.data[p * 4 + k], k < ch ? px[p * ch + k] : 255, `pixel ${p} channel ${k}`);
});

test("readPng refuses what it cannot read", () => {
  assert.throws(() => readPng(Buffer.from("not a png at all")), /not a PNG/);
  const bad = encodeFiltered(2, 2, 3, pattern(12, 0)); bad[8 + 8 + 8] = 16;   // IHDR bit depth 16
  assert.throws(() => readPng(bad), /only 8-bit/);
  const cut = encodeFiltered(2, 2, 3, pattern(12, 0)).subarray(0, 48);   // IDAT cut short
  assert.throws(() => readPng(cut), /past the end/);
});
