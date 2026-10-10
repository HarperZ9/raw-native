// node --test web/shaders/pixel.test.mjs
// Perspective-stable pixel art on its CPU reference. GPU parity: tests/web/shaders_parity.py --shader pixel.
import test from "node:test";
import assert from "node:assert/strict";
import { captureProbe, shadeProbe, snapOrigin } from "./pixel/probe.mjs";
import { camera, splat, resolve } from "./pixel/splat.mjs";
import { pixelise, splatPositions, mismatch } from "./pixel/stability.mjs";
import { createPixel } from "./pixel/pixel.mjs";
import { linearToSrgb } from "./common.mjs";
import { integerUpscale, sharpBilinear } from "./pixel/scale.mjs";

const W = 160, H = 100, N = 48, cell = 0.5;
const eyeAt = (a) => [6.5 * Math.sin(a), 4.2, 6.5 * Math.cos(a)];
const enc = (img) => img.map((v, i) => ((i & 3) === 3 ? 1 : linearToSrgb(v)));
function pair(mk) {
  const [eA, tA] = mk(0.6), [eB, tB] = mk(0.604), cA = camera(eA, tA, 0.75, W, H), cB = camera(eB, tB, 0.75, W, H);
  const pr = captureProbe(snapOrigin(eA, cell), N), col = shadeProbe(pr);
  assert.deepEqual(snapOrigin(eB, cell), pr.origin, "both views in one cell");
  const vA = splat(pr, cA), vB = splat(pr, cB);
  const s = mismatch(cA, enc(resolve(cA, { vis: vA, col })), splatPositions(pr, vA, W, H), cB, enc(resolve(cB, { vis: vB, col })), splatPositions(pr, vB, W, H));
  const nA = pixelise(cA, 3.75), nB = pixelise(cB, 3.75), n = mismatch(cA, enc(nA.img), nA.pos, cB, enc(nB.img), nB.pos);
  return { splat: s.fraction, naive: n.fraction, compared: s.compared };
}

test("within a cell, texels keep their colour as the camera orbits, turns and dollies; naive pixelisation shimmers", () => {
  for (const [label, mk] of [["orbit", (a) => [eyeAt(a), [0, 0.6, 0]]], ["turn", (a) => [eyeAt(0.6), [Math.sin(a - 0.6) * 7, 0.6, 0]]],
    ["dolly", (a) => [eyeAt(0.6).map((v) => v * (1 - (a - 0.6) * 5)), [0, 0.6, 0]]]]) {
    const r = pair(mk);
    assert.ok(r.compared > 0.5 * W * H, `${label}: enough pixels compared (${r.compared})`);
    assert.ok(r.splat < 0.002 && r.naive > 0.03, `${label}: splat ${r.splat}, naive ${r.naive}`);
  }
});

test("a cell change crossfades: the first frame after it moves only about a twelfth of the pixels", () => {
  const px = createPixel({ N, cell }, { w: W, h: H });
  let a = 0.6, before = null;
  for (; a < 1.2; a += 0.004) {
    const r = px.frame({ eye: eyeAt(a), target: [0, 0.6, 0] });
    if (before && r.blend < 1) {
      let changed = 0; for (let p = 0; p < W * H; p++) if (Math.abs(r.img[p * 4] - before.img[p * 4]) > 2 / 255) changed++;
      assert.ok(changed / (W * H) < 0.2, `first crossfade frame changed ${changed / (W * H)} of the pixels`);
      return;
    }
    before = r;
  }
  assert.fail("no cell change found in the sweep");
});

test("texels cover the view: eye-ray fills are a small part of the frame", () => {
  const cam = camera(eyeAt(0.6), [0, 0.6, 0], 0.75, W, H), pr = captureProbe(snapOrigin(eyeAt(0.6), cell), N), vis = splat(pr, cam);
  let holes = 0; for (const v of vis.idb) if (v === 0xffffffff) holes++;
  assert.ok(holes / (W * H) < 0.05, `holes ${holes / (W * H)}`);
});

test("integer upscale is nearest-neighbour exactly; sharp-bilinear keeps texel interiors exact and blends only at edges", () => {
  const src = { width: 4, height: 3, data: new Uint8Array(4 * 3 * 4).map((_, i) => (i * 37) & 255) };
  const up = integerUpscale(src, 3);
  for (let y = 0; y < 9; y++) for (let x = 0; x < 12; x++) for (let c = 0; c < 4; c++) assert.equal(up.data[(y * 12 + x) * 4 + c], src.data[(((y / 3) | 0) * 4 + ((x / 3) | 0)) * 4 + c]);
  const sb = sharpBilinear(src, 10, 7, 0, 0);
  assert.equal(sb.width, 10);
  for (let c = 0; c < 3; c++) assert.equal(sb.data[(3 * 10 + 4) * 4 + c], src.data[(1 * 4 + 1) * 4 + c], "an interior pixel is its texel exactly");
});

test("sharp-bilinear with a subpixel offset pans smoothly: no edge jumps more than one output pixel per 0.1-texel step", () => {
  const src = { width: 8, height: 1, data: new Uint8Array(32) }; for (let x = 0; x < 8; x++) { const v = x < 4 ? 0 : 255; src.data.set([v, v, v, 255], x * 4); }
  let prevEdge = null;
  for (let s = 0; s <= 10; s++) {
    const o = sharpBilinear(src, 40, 1, s / 10, 0); let edge = 0;
    for (let x = 0; x < 40; x++) edge += o.data[x * 4] / 255;
    if (prevEdge !== null) assert.ok(Math.abs(edge - prevEdge) <= 1.0001, `step ${s}: coverage moved ${edge - prevEdge}`);
    prevEdge = edge;
  }
});
