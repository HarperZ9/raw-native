// node --test web/shaders/paint.test.mjs
// Properties of the painterly model on its CPU reference. GPU parity: tests/web/shaders_parity.py --shader paint.
import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { kmDecode, latentLut, buildLut } from "./paint/pigments.mjs";
import { prep } from "./paint/abstract.mjs";
import { glaze } from "./paint/compose.mjs";
import { createPaint, resolvePaint } from "./paint/paint.mjs";

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b} (tol ${tol})`);
const frame = (w, h, f) => { const d = new Float32Array(w * h * 4); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) d.set([...f(x, y), 1], (y * w + x) * 4); return { width: w, height: h, data: d }; };

test("Kubelka-Munk mixing: blue and yellow make green, yellow and magenta make orange", () => {
  const g = kmDecode([0, 0.5, 0, 0.5]), o = kmDecode([0, 0.5, 0.5, 0]);
  assert.ok(g[1] > g[0] && g[1] > g[2], `blue + yellow ${g}`);
  assert.ok(o[0] > o[1] && o[1] > o[2], `yellow + magenta ${o}`);
});

test("the shipped pigment lookup is the one the solver produces", () => {
  const sha = (a) => createHash("sha256").update(Buffer.from(a.buffer)).digest("hex");
  assert.equal(sha(latentLut()), sha(buildLut()));
});

test("an unmixed colour decodes back to itself (the residual closes the latent)", () => {
  const plan = resolvePaint("oil", {}, { w: 16, h: 8 }), sc = frame(16, 8, (x, y) => [x / 15, y / 7, 0.3 + 0.02 * x]);
  const { lat } = prep(plan, sc);
  for (let i = 0; i < 16 * 8; i++) {
    const dec = kmDecode([lat[i * 8], lat[i * 8 + 1], lat[i * 8 + 2], lat[i * 8 + 3]]);
    for (let c = 0; c < 3; c++) near(dec[c] + lat[i * 8 + 4 + c], 1 - Math.exp(-sc.data[i * 4 + c] * plan.p.exposure), 2e-6, `pixel ${i} channel ${c}`);
  }
});

test("a glaze tends to the paper when thin and to the pigment's masstone when thick", () => {
  const c = [0.1, 0.3, 0.2, 0.4], thin = glaze(c, 1e-6, 0.8), thick = glaze(c, 60, 0.8), mass = kmDecode(c);
  thin.forEach((v) => near(v, 0.8, 1e-3, "thin glaze"));
  thick.forEach((v, k) => near(v, mass[k], 1e-3, "thick glaze"));
});

test("the Kuwahara filter keeps a flat field and does not blur across a hard edge", () => {
  const W = 40, H = 24, sc = frame(W, H, (x) => (x < 20 ? [0.05, 0.1, 0.4] : [0.6, 0.45, 0.05]));
  const r = createPaint("oil", {}, { w: W, h: H }).frame(sc);
  for (const x of [8, 16, 23, 32]) {
    const L = r.ak.lat.slice((12 * W + x) * 8, (12 * W + x) * 8 + 7), dec = kmDecode([L[0], L[1], L[2], L[3]]), want = x < 20 ? [0.05, 0.1, 0.4] : [0.6, 0.45, 0.05];
    want.forEach((v, c) => near(dec[c] + L[4 + c], 1 - Math.exp(-v * 2.2), 0.02, `x ${x} channel ${c}`));
  }
});

// Temporal coherence under a camera pan (roadmap S9's flicker metric): pan a scene by one pixel
// a frame and compare each frame with the next one shifted back. With the canvas offset
// following the pan, the paint moves with the world; without it, strokes swim.
function panFlicker(anchored) {
  const W = 72, H = 40, scene = (dx) => frame(W, H, (x, y) => { const u = x + dx; return [0.3 + 0.25 * Math.sin(u * 0.21) * Math.cos(y * 0.17), 0.25 + 0.2 * Math.sin(u * 0.13 + y * 0.11), 0.2 + 0.15 * Math.cos(u * 0.09 - y * 0.2)]; });
  const out = (dx) => createPaint("oil-grotesque", { canvasOffset: anchored ? [dx, 0] : [0, 0] }, { w: W, h: H }).frame(scene(dx)).img;
  const a = out(0), b = out(1); let s = 0, n = 0;
  for (let y = 12; y < H - 12; y++) for (let x = 12; x < W - 13; x++) for (let c = 0; c < 3; c++) { s += Math.abs(a[(y * W + x + 1) * 4 + c] - b[(y * W + x) * 4 + c]); n++; }
  return s / n;
}
test("panning: with the canvas anchored, consecutive frames agree far more than without", () => {
  const fixed = panFlicker(true), swim = panFlicker(false);
  assert.ok(fixed < 0.25 * swim, `mean frame-to-frame difference anchored ${fixed}, screen-locked ${swim}`);
  assert.ok(fixed < 0.01, `anchored difference ${fixed}`);
});
