// PNG read and write for 8-bit gray, RGB and RGBA, with node:zlib. Enough for maps and review sheets.
import { deflateSync, inflateSync } from "node:zlib";

const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc32 = (b) => { let c = 0xffffffff; for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const out = Buffer.alloc(12 + data.length); out.writeUInt32BE(data.length, 0); out.write(type, 4, "latin1"); Buffer.from(data).copy(out, 8); out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length); return out; };

// data: Uint8Array of width * height * channels, channels 1, 3 or 4
export function encodePng({ width, height, channels = 4, data }) {
  const ct = { 1: 0, 3: 2, 4: 6 }[channels]; if (ct === undefined) throw new Error("png: channels must be 1, 3 or 4");
  const row = width * channels, raw = Buffer.alloc((row + 1) * height);
  for (let y = 0; y < height; y++) { raw[y * (row + 1)] = 0; Buffer.from(data.buffer, data.byteOffset + y * row, row).copy(raw, y * (row + 1) + 1); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = ct;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw, { level: 6 })), chunk("IEND", Buffer.alloc(0))]);
}

export function decodePng(buf) {
  const b = Buffer.from(buf); let p = 8, w = 0, h = 0, ct = 0; const idat = [];
  while (p < b.length) { const len = b.readUInt32BE(p), type = b.toString("latin1", p + 4, p + 8), body = b.subarray(p + 8, p + 8 + len); if (type === "IHDR") { w = body.readUInt32BE(0); h = body.readUInt32BE(4); if (body[8] !== 8 || body[12] !== 0) throw new Error("png: only 8-bit, non-interlaced"); ct = body[9]; } else if (type === "IDAT") idat.push(body); p += 12 + len; }
  const ch = { 0: 1, 2: 3, 6: 4 }[ct]; if (!ch) throw new Error(`png: colour type ${ct}`);
  const raw = inflateSync(Buffer.concat(idat)), row = w * ch, out = new Uint8Array(row * h);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (row + 1)], src = y * (row + 1) + 1;
    for (let x = 0; x < row; x++) {
      const a = x >= ch ? out[y * row + x - ch] : 0, up = y ? out[(y - 1) * row + x] : 0, c = x >= ch && y ? out[(y - 1) * row + x - ch] : 0;
      let v = raw[src + x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pa = Math.abs(up - c), pb = Math.abs(a - c), pc = Math.abs(a + up - 2 * c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      out[y * row + x] = v & 255;
    }
  }
  return { width: w, height: h, channels: ch, data: out };
}
