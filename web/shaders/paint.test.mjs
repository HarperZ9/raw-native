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

// Advected canvas (canvas.mjs): motion vectors carry the paper and brushwork under rotation, zoom and parallax.
import { createCanvasState, decide, advect, layerWeights, CANVAS } from "./paint/canvas.mjs";
import { advectedCanvas, offsetCanvas } from "./paint/paint.mjs";
import { paperHeight } from "./paint/noise.mjs";
import { streetScene, motionVectors } from "./fixtures/street.mjs";

test("a 2D pan through motion vectors moves the canvas exactly and never regenerates a layer", () => {
  const W = 48, H = 32, st = createCanvasState(W, H), mv = new Float32Array(W * H * 2), d = new Float32Array(W * H).fill(5);
  for (let i = 0; i < W * H; i++) mv[i * 2] = 2;
  decide(st); advect(st, new Float32Array(W * H * 2), d, d);
  const C0 = st.C.slice();
  for (let f = 0; f < 10; f++) { decide(st); advect(st, mv, d, d); }
  let checked = 0;
  for (let y = 0; y < H; y++) for (let x = 22; x < W; x++) { near(st.C[(y * W + x) * 4], C0[(y * W + x - 20) * 4], 1e-3, "x coordinate"); checked++; }
  assert.ok(checked > 500, "the overlap was checked");
  for (let y = 4; y < H - 4; y++) near(st.C[(y * W + 40) * 4 + 1], C0[(y * W + 40) * 4 + 1], 1e-3, "y coordinate unchanged by a horizontal pan");
  assert.equal(st.layer[1].age, -1, "the second layer never woke");
});

function swim(camera, frames, W = 160, H = 100) {
  const sc = []; for (let f = 0; f < frames; f++) sc.push(streetScene(W, H, 0, camera(f)));
  const plan = resolvePaint("oil", {}, { w: W, h: H }), st = createCanvasState(W, H), A = [], S = [];
  for (let f = 0; f < frames; f++) {
    const m = f ? motionVectors(sc[f - 1], sc[f]) : { mv: new Float32Array(W * H * 2), dA: sc[0].depth };
    decide(st); advect(st, m.mv, sc[f].depth, m.dA);
    const ca = advectedCanvas(st), cs = offsetCanvas(plan), a = new Float32Array(W * H), s = new Float32Array(W * H);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { a[y * W + x] = ca.n(paperHeight, x, y); s[y * W + x] = cs.n(paperHeight, x, y); }
    A.push(a); S.push(s);
  }
  const score = (out) => { let sum = 0, n = 0;
    for (let f = 1; f < frames; f++) { const { mv, dA } = motionVectors(sc[f - 1], sc[f]);
      for (let y = 3; y < H - 3; y++) for (let x = 3; x < W - 3; x++) { const p = y * W + x, px = x - mv[p * 2], py = y - mv[p * 2 + 1];
        if (px < 1 || py < 1 || px >= W - 2 || py >= H - 2) continue; const q = Math.round(py) * W + Math.round(px);
        if (Math.abs(sc[f - 1].depth[q] - dA[p]) > 0.02 * dA[p]) continue;
        const x0 = Math.floor(px), y0 = Math.floor(py), tx = px - x0, ty = py - y0, g = (xx, yy) => out[f - 1][yy * W + xx];
        sum += Math.abs(out[f][p] - ((g(x0, y0) * (1 - tx) + g(x0 + 1, y0) * tx) * (1 - ty) + (g(x0, y0 + 1) * (1 - tx) + g(x0 + 1, y0 + 1) * tx) * ty)); n++; } }
    return sum / n; };
  return { advected: score(A), screen: score(S), st };
}
test("orbit and dolly at 160 x 100: the advected paper swims under 0.6 times as much as a screen-locked one", () => {
  const R = Math.hypot(5.2, 6.4);
  for (const [name, cam] of [["orbit", (f) => { const a = 0.69 + f * 0.01; return { eye: [R * Math.sin(a), 4.6, R * Math.cos(a)] }; }],
    ["dolly", (f) => ({ eye: [5.2 * (1 - 0.012 * f), 4.6 * (1 - 0.012 * f), 6.4 * (1 - 0.012 * f)] })]]) {
    const r = swim(cam, 8);
    assert.ok(r.advected < 0.6 * r.screen, `${name}: advected ${r.advected}, screen-locked ${r.screen}`);
  }
});
test("a long dolly regenerates a layer and keeps the weighted distortion under the threshold", () => {
  const r = swim((f) => ({ eye: [5.2 * (1 - 0.012 * f), 4.6 * (1 - 0.012 * f), 6.4 * (1 - 0.012 * f)] }), 30, 96, 60), L = r.st.layer, w = layerWeights(r.st);
  assert.ok(L[1].age >= 0, "the second layer woke");
  const wd = (w[0] * L[0].meanD + w[1] * L[1].meanD) / (w[0] + w[1]);
  assert.ok(wd < CANVAS.tau, `weighted distortion ${wd}`);
});
