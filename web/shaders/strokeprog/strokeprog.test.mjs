import test from "node:test";
import assert from "node:assert/strict";
import { paint } from "../paintover/painter.mjs";
import { renderProgram, packProgram, unpackProgram, binStrokes, makeUnderpaint, blur, STROKE_BYTES } from "./strokeprog.mjs";

const W = 96, H = 64, meta = { W, H, seed: 3, radii: [11, 6, 3.5, 2], relief: 0.2 };
function scene() {
  const rgb = new Float32Array(W * H * 3), id = new Uint8Array(W * H), N = new Float32Array(W * H * 3);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const i = y * W + x, left = x < 40; id[i] = left ? 1 : 2; rgb[i * 3] = left ? 0.8 : 0.2 + x / 400; rgb[i * 3 + 1] = 0.4 + y / 300; rgb[i * 3 + 2] = left ? 0.2 : 0.7; N[i * 3 + 1] = 1; }
  return { rgb, id, N };
}
test("captured strokes re-render the painter's own frame (exact floats, exact underpaint)", () => {
  const s = scene(), rec = [], dbg = {}, res = paint({ w: W, h: H, rgb: s.rgb }, { id: s.id, N: s.N }, { ...meta, ink: false, record: rec, debug: dbg });
  assert.ok(rec.length > 20);
  const planes = [0, 1, 2].map((c) => { const p = new Float32Array(W * H); for (let i = 0; i < W * H; i++) p[i] = s.rgb[i * 3 + c]; return blur(p, W, H, 9); });
  const r = renderProgram(rec, meta, { ids: { data: s.id, w: W, h: H }, canvas0: planes });
  let max = 0; for (let i = 0; i < W * H * 4; i++) max = Math.max(max, Math.abs(r.rgba8[i] - res.rgba8[i]));
  assert.ok(max <= 1, `max level difference ${max}`);
});
test("compact form: 13 bytes a stroke, round trip keeps order, layers and object ids", () => {
  const s = scene(), rec = []; paint({ w: W, h: H, rgb: s.rgb }, { id: s.id, N: s.N }, { ...meta, ink: false, record: rec });
  const bin = packProgram(rec, meta), back = unpackProgram(bin);
  assert.equal(bin.length, 16 + 16 + rec.length * STROKE_BYTES);
  assert.equal(back.strokes.length, rec.length);
  rec.forEach((k, i) => { assert.equal(back.strokes[i].layer, k.layer); assert.equal(back.strokes[i].sid, k.sid); assert.ok(Math.abs(back.strokes[i].ax - k.ax) <= 1 / 16 + 1e-9); });
});
test("tile bins keep paint order and cover every stroke", () => {
  const s = scene(), rec = []; paint({ w: W, h: H, rgb: s.rgb }, { id: s.id, N: s.N }, { ...meta, ink: false, record: rec });
  const b = binStrokes(rec, W, H, 16);
  for (let t = 0; t < b.tx * b.ty; t++) for (let q = b.offsets[t] + 1; q < b.offsets[t + 1]; q++) assert.ok(b.list[q] > b.list[q - 1]);
  const seen = new Set(b.list); assert.equal(seen.size, rec.length);
});
test("output scale 2 keeps the picture: area-down of the 2x frame is close to the 1x frame", () => {
  const s = scene(), rec = []; paint({ w: W, h: H, rgb: s.rgb }, { id: s.id, N: s.N }, { ...meta, ink: false, record: rec });
  const u = makeUnderpaint(s.rgb, W, H), ids = { data: s.id, w: W, h: H };
  const a = renderProgram(rec, meta, { ids, under: u }), b = renderProgram(rec, meta, { ids, under: u, scale: 2 });
  let se = 0, n = 0; for (let y = 4; y < H - 4; y++) for (let x = 4; x < W - 4; x++) for (let c = 0; c < 3; c++) { let m = 0; for (let k = 0; k < 4; k++) m += b.rgba8[((2 * y + (k >> 1)) * 2 * W + 2 * x + (k & 1)) * 4 + c] / 4; const d = m - a.rgba8[(y * W + x) * 4 + c]; se += d * d; n++; }
  assert.ok(10 * Math.log10(65025 / (se / n)) > 25);
});
