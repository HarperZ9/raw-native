// The vector corpus of evidence/m1-vector-corpus.json as Motion items, and its CPU
// reference images. Shared by the browser page (web/test/vector-corpus.html) and
// Node (this file run directly writes the reference images to a directory).
//
//   node tests/web/vector_corpus.mjs OUT_DIR     writes <id>.rgba.f32 and <id>.cls.u8 per case
import { parsePath } from "../../web/motion/path.mjs";

export const TOLERANCE = 0.1;

// The checker16 image of the corpus: 16 x 16 RGBA8.
export function checker16() {
  const data = new Uint8Array(16 * 16 * 4), a = [0xe8, 0xe4, 0xda, 0xff], b = [0x1b, 0x2a, 0x4a, 0xff];
  for (let j = 0; j < 16; j++) for (let i = 0; i < 16; i++) data.set((i + j) % 2 === 0 ? a : b, 4 * (j * 16 + i));
  return { width: 16, height: 16, data };
}

export function items(corpus) {
  return corpus.cases.map((c) => ({
    id: c.id,
    item: {
      shape: parsePath(c.shape, { tolerance: TOLERANCE }), fill: c.fill ?? null, stroke: c.stroke ?? null, width: c.width,
      rule: c.rule, join: c.join, cap: c.cap, miterLimit: c.miterLimit, dash: c.dash, dashOffset: c.dashOffset,
      clip: c.clip ? parsePath(c.clip, { tolerance: TOLERANCE }) : undefined, clipRule: c.clipRule,
    },
  }));
}

if (globalThis.process?.argv?.[1]?.endsWith("vector_corpus.mjs")) {
  const { readFileSync, writeFileSync, mkdirSync } = await import("node:fs");
  const { reference } = await import("../../web/motion/vector_ref.mjs");
  const corpus = JSON.parse(readFileSync(new URL("../../evidence/m1-vector-corpus.json", import.meta.url), "utf8"));
  const out = process.argv[2];
  mkdirSync(out, { recursive: true });
  const [W, H] = corpus.canvas.size, images = { checker16: checker16() };
  for (const { id, item } of items(corpus)) {
    const t0 = performance.now();
    const r = reference(item, { width: W, height: H, images });
    writeFileSync(`${out}/${id}.rgba.f32`, Buffer.from(r.rgba.buffer));
    writeFileSync(`${out}/${id}.cls.u8`, Buffer.from(r.cls.buffer));
    console.log(id, (performance.now() - t0).toFixed(0), "ms");
  }
}
