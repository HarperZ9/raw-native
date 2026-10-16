// The .rnfx fixture bundle the C++ style checks read (src/renderer/gpu/style_fixture.hpp): the 8
// bytes "RNFX0001", a little-endian u32 header length, the header as UTF-8 JSON, zero padding to a
// multiple of 8, then the arrays. The header names each array with its type (u8, u32, f32, f64),
// element count and byte offset from the start of the array block.
import { readFileSync, writeFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
export const readJson = (p) => JSON.parse(readFileSync(join(ROOT, p), "utf8"));

export class Bundle {
  constructor(pass) { this.head = { pass, arrays: {} }; this.blobs = []; this.size = 0; }
  add(name, type, arr) {
    const T = { u8: Uint8Array, u32: Uint32Array, f32: Float32Array, f64: Float64Array }[type];
    const a = arr instanceof T ? arr : T.from(arr), bytes = new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
    this.head.arrays[name] = { type, count: a.length, offset: this.size };
    this.blobs.push(bytes); this.size += bytes.length;
    const pad = (8 - (this.size % 8)) % 8; if (pad) { this.blobs.push(new Uint8Array(pad)); this.size += pad; }
  }
  write(path) {
    const head = new TextEncoder().encode(JSON.stringify(this.head)), lenPad = (8 - ((12 + head.length) % 8)) % 8;
    const out = new Uint8Array(12 + head.length + lenPad + this.size), dv = new DataView(out.buffer);
    out.set(new TextEncoder().encode("RNFX0001"), 0); dv.setUint32(8, head.length, true); out.set(head, 12);
    let o = 12 + head.length + lenPad; for (const b of this.blobs) { out.set(b, o); o += b.length; }
    writeFileSync(path, out);
  }
}
