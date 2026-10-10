// node --test web/shaders/vhs.test.mjs
// VHS on its CPU reference. GPU parity: tests/web/shaders_parity.py --shader vhs.
import test from "node:test";
import assert from "node:assert/strict";
import { createVhs, lineShift } from "./vhs/vhs.mjs";

const quiet = { lumaNoise: 0, chromaNoise: 0, jitter: 0, dropouts: 0, phaseNoiseDeg: 0, peaking: 0, headSkew: 0, tracking: 0, generations: 1 };
const frame = (w, h, f) => { const d = new Float32Array(w * h * 4); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) d.set([...f(x, y), 1], (y * w + x) * 4); return { width: w, height: h, data: d }; };
const row = (sig, N, y, c) => Array.from({ length: N }, (_, k) => sig[(y * N + k) * 4 + c]);

test("with every loss off, a flat field passes the tape unchanged", () => {
  const v = createVhs("sp-fresh", quiet, { w: 64, h: 8 }), out = v.frame(frame(64, 8, () => [0.4, 0.55, 0.3]), 0).sig;
  for (let k = 20; k < v.plan.N - 20; k++) [0.4, 0.55, 0.3].forEach((t, c) => assert.ok(Math.abs(out[(4 * v.plan.N + k) * 4 + c] - t) < 1e-3, `sample ${k} channel ${c}`));
});

test("luma bandwidth: 1 MHz detail survives, 4 MHz detail is mostly gone (SP, about 3 MHz)", () => {
  const amp = (mhz) => {
    const W = 360, v = createVhs("sp-fresh", quiet, { w: W, h: 4 }), period = 13.5 / mhz;   // source pixels per cycle at 13.5 MHz sampling
    const s = v.frame(frame(W, 4, (x) => { const g = 0.5 + 0.3 * Math.sin((2 * Math.PI * x) / period); return [g, g, g]; }), 0).sig;
    // Amplitude of the test frequency itself (a DFT projection over the middle of the line), so the
    // source's own sample-and-hold images, which beat against the 4 fsc sampling, do not count.
    const r = row(s, v.plan.N, 2, 1).slice(60, -60), fn = (mhz / (4 * v.plan.p.fsc)) * 2 * Math.PI;
    let re = 0, im = 0; r.forEach((q, k) => { re += q * Math.cos(fn * k); im += q * Math.sin(fn * k); });
    return (2 * Math.hypot(re, im) / r.length) / 0.3;
  };
  const a1 = amp(1), a4 = amp(4);
  assert.ok(a1 > 0.8 && a4 < 0.3, `1 MHz ${a1}, 4 MHz ${a4}`);
});

test("colour smears far wider than luma, and lags it by the chroma delay", () => {
  const W = 200, v = createVhs("sp-fresh", quiet, { w: W, h: 4 });
  const s = v.frame(frame(W, 4, (x) => (x < 100 ? [0.2, 0.2, 0.2] : [0.7, 0.2, 0.2])), 0).sig, N = v.plan.N;
  // 10-90 rise between the settled levels on each side, searched outward from the 50% crossing,
  // so filter ripple far from the edge cannot cross a threshold early.
  const width = (vals) => {
    const L0 = vals.slice(5, 25).reduce((a, b) => a + b, 0) / 20, L1 = vals.slice(-25, -5).reduce((a, b) => a + b, 0) / 20, at = (f) => L0 + f * (L1 - L0);
    const c = vals.findIndex((q, i) => i > 25 && q > at(0.5));
    let a = c; while (a > 0 && vals[a] > at(0.1)) a--;
    let b = c; while (b < vals.length - 1 && vals[b] < at(0.9)) b++;
    return [b - a, c];
  };
  const Y = row(s, N, 2, 0).map((r, k) => 0.299 * r + 0.587 * s[(2 * N + k) * 4 + 1] + 0.114 * s[(2 * N + k) * 4 + 2]);
  const I = row(s, N, 2, 0).map((r, k) => r - Y[k]);
  const [wy, cy] = width(Y), [wc, cc] = width(I);
  assert.ok(wc > 3 * wy, `chroma rise ${wc} samples, luma rise ${wy}`);
  assert.ok(Math.abs(cc - cy - v.plan.delay) <= 1.5, `chroma lags luma by ${cc - cy} samples, delay ${v.plan.delay}`);
});

test("dropouts happen at about the set rate and are filled from the line above", () => {
  const v = createVhs("ep-rental", { ...quiet, dropouts: 0.05 }, { w: 256, h: 120 }), f = frame(256, 120, (x, y) => [y / 120, 0.3, 0.3]);
  const s = v.frame(f, 4).sig; let dropped = 0; for (let i = 3; i < s.length; i += 4) dropped += s[i];
  const rate = dropped / (v.plan.N * 120), expected = 0.05 * v.plan.g * 0.5;   // a hit segment loses half of it on average
  assert.ok(rate > 0.5 * expected && rate < 1.6 * expected, `dropout fraction ${rate}, expected about ${expected}`);
});

test("the head switch skews the bottom lines, more toward the bottom, and nothing above", () => {
  const plan = createVhs("sp-fresh", { jitter: 0, tracking: 0, headSkew: 6, headSwitchLines: 6 }, { w: 64, h: 240 }).plan;
  assert.ok(Math.abs(lineShift(plan, 100, 0)) < 1e-12, "no shift above the switch");
  const a = lineShift(plan, 236, 0), b = lineShift(plan, 239, 0);
  assert.ok(b > a && a > 0 && b <= 6, `skew ${a} then ${b}`);
});

test("a frame is reproducible, and the next frame's noise is different", () => {
  const v = createVhs("thriller", {}, { w: 96, h: 40 }), f = frame(96, 40, (x, y) => [x / 96, y / 40, 0.4]);
  const a = v.frame(f, 9).sig, b = v.frame(f, 9).sig, c = v.frame(f, 10).sig;
  assert.deepEqual(Array.from(a), Array.from(b));
  let diff = 0; for (let i = 0; i < a.length; i++) diff += Math.abs(a[i] - c[i]);
  assert.ok(diff / a.length > 1e-3, "noise moves between frames");
});
