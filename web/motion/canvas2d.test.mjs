// drawFrame on a recording 2D context: where things land, and which state each item sets.
// The pixel comparison with the CPU reference is tests/web/canvas2d_corpus.py (in a browser).
import test from "node:test";
import assert from "node:assert/strict";
import { drawFrame, toPath } from "./canvas2d.mjs";
import { rect } from "./path.mjs";

class RecPath { constructor() { this.ops = []; } moveTo(x, y) { this.ops.push(["M", x, y]); } lineTo(x, y) { this.ops.push(["L", x, y]); } closePath() { this.ops.push(["Z"]); } }
function recorder(W = 960, H = 540) {
  const log = [], st = { globalAlpha: 1, globalCompositeOperation: "source-over", filter: "none", lineWidth: 1 }, stack = [];
  const g = new Proxy(st, {
    get(t, k) {
      if (k in t) return t[k];
      if (k === "canvas") return { width: W, height: H };
      if (k === "save") return () => stack.push({ ...st });
      if (k === "restore") return () => Object.assign(st, stack.pop());
      if (k === "createRadialGradient" || k === "createLinearGradient") return (...a) => ({ kind: k, a, stops: [], addColorStop(o, c) { this.stops.push([o, c]); } });
      return (...a) => log.push({ op: k, a, state: { ...st, lineDash: st._dash } });
    },
    set(t, k, v) { t[k] = v; return true; },
  });
  st.setLineDash = (d) => { st._dash = d; };
  return { g, log };
}
const draws = (log) => log.filter((e) => e.op === "fill" || e.op === "stroke");

test("the camera maps scene units to pixels, with depth parallax; screen items ignore it", () => {
  const { g, log } = recorder(960, 540);
  const list = { camera: { x: 960, y: 540, zoom: 2 }, items: [
    { shape: rect(950, 530, 20, 20), fill: "#ffffff" },
    { shape: rect(950, 530, 20, 20), fill: "#ffffff", z: 1000 },
    { shape: rect(0, 0, 100, 50), fill: "#ffffff", screen: true },
  ] };
  const stats = drawFrame(g, list, { width: 960, height: 540, vignette: false, makePath: () => new RecPath() });
  assert.equal(stats.items, 3);
  const [a, b, c] = draws(log).map((e) => e.a[0].ops);
  assert.deepEqual(a[0], ["M", 480 - 10, 270 - 10]);       // half size canvas, zoom 2: 20 units -> 20 px
  assert.deepEqual(b[0], ["M", 480 - 5, 270 - 5]);         // at z = distance, half the scale
  assert.deepEqual(c[0], ["M", 0, 0]);
  assert.deepEqual(c[2], ["L", 50, 25]);                   // screen space at canvas / design = 0.5
});

test("opacity, additive blending, the fill rule and stroke style reach the context", () => {
  const { g, log } = recorder(1920, 1080);
  drawFrame(g, { items: [
    { shape: rect(0, 0, 10, 10), fill: [1, 0, 0, 1], opacity: 0.5, blend: "add", rule: "evenodd" },
    { shape: rect(0, 0, 10, 10), stroke: "#00ff00", width: 6, join: "miter", cap: "butt", dash: [4, 2], dashOffset: 1 },
  ] }, { vignette: false, makePath: () => new RecPath() });
  const [f, s] = draws(log);
  assert.equal(f.state.globalAlpha, 0.5);
  assert.equal(f.state.globalCompositeOperation, "lighter");
  assert.equal(f.a[1], "evenodd");
  assert.equal(f.state.fillStyle, "rgba(255,0,0,1)");
  assert.equal(s.state.lineWidth, 6);
  assert.equal(s.state.lineJoin, "miter");
  assert.equal(s.state.lineCap, "butt");
  assert.deepEqual(s.state.lineDash, [4, 2]);
  assert.equal(s.state.lineDashOffset, 1);
  assert.equal(s.state.strokeStyle, "rgba(0,255,0,1)");
});

test("gradients are placed in pixels; layers it cannot draw are counted, not drawn", () => {
  const { g, log } = recorder(960, 540);
  const stats = drawFrame(g, { items: [
    { shape: rect(0, 0, 100, 100), fill: { linear: [0, 0, 100, 0], stops: [[0, "#000000"], [1, "#ffffff"]] } },
    { kind: "particles" }, { kind: "threads" }, { kind: "threads" },
  ], post: { passes: [{ pass: "paint" }] } }, { vignette: false, makePath: () => new RecPath() });
  const fill = draws(log)[0];
  assert.equal(fill.state.fillStyle.kind, "createLinearGradient");
  assert.deepEqual(fill.state.fillStyle.a, [0, 0, 50, 0]);
  assert.equal(fill.state.fillStyle.stops.length, 2);
  assert.deepEqual(stats.skipped, { particles: 1, threads: 2, post: 1 });
});

test("a null background clears; a colour fills; the vignette darkens only at the edges", () => {
  const a = recorder(), b = recorder();
  const paint = (log) => log.filter((e) => e.op !== "setTransform");
  drawFrame(a.g, { background: null, items: [] }, { vignette: false });
  assert.equal(paint(a.log)[0].op, "clearRect");
  drawFrame(b.g, { background: [0.5, 0.5, 0.5], items: [] }, {});
  const [bgFill, vigFill] = paint(b.log);
  assert.equal(bgFill.op, "fillRect");
  assert.equal(bgFill.state.fillStyle, "rgba(128,128,128,1)");
  const vig = vigFill.state.fillStyle;
  assert.equal(vig.kind, "createRadialGradient");
  assert.equal(vig.stops[0][1], "rgba(0,0,0,0.0000)");
  assert.ok(Number(vig.stops.at(-1)[1].match(/[\d.]+\)$/)[0].slice(0, -1)) > 0.2);
});

test("near-duplicate points are dropped so a butt cap keeps the stroke's direction", () => {
  const p = toPath([{ pts: Float32Array.from([0, 0, 10, 0, 10.001, 0]), closed: false }], 1, 0, 0, () => new RecPath());
  assert.equal(p.ops.length, 2);
});
