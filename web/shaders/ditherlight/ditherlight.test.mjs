import test from "node:test";
import assert from "node:assert/strict";
import { BAYER } from "../reference/retro-dither.mjs";
import { bayer8Rank, blueRank, thresholdAt, anchorCell, octaveOf, pixelAngle } from "./thresholds.mjs";
import { makeRadial, makeBeam, maskAt, surfaceTerm } from "./lights.mjs";
import { resolveDitherlight, quantise, toneOf, levelLinear, levelRgb8, runDitherlight } from "./ditherlight.mjs";
import { renderFrame, pathSpecs, lightsOf, project } from "../fixtures/dream.mjs";

test("bayer8Rank equals the vendored Bayer 8 table in every cell", () => {
  for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) assert.equal(bayer8Rank(x, y), BAYER[8][y][x], `${x},${y}`);
});
test("both threshold sources are permutations of their ranks, negative cells wrap", () => {
  const b = new Set(), n = new Set();
  for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) b.add(bayer8Rank(x, y));
  for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) n.add(blueRank(x, y));
  assert.equal(b.size, 64); assert.equal(n.size, 4096);
  assert.equal(bayer8Rank(-1, -3), bayer8Rank(7, 5)); assert.equal(blueRank(-1, -3), blueRank(63, 61));
  for (const s of ["bayer8", "blue64"]) for (let i = 0; i < 50; i++) { const t = thresholdAt(s, i * 7, i * 13); assert.ok(t > 0 && t < 1); }
});
test("mean preservation: over one tile the dithered linear light equals the tone to within half a rank", () => {
  for (const levels of [2, 3, 4, 8]) for (const source of ["bayer8", "blue64"]) {
    const plan = resolveDitherlight("ink", { levels, source }), M = source === "bayer8" ? 64 : 4096, S = source === "bayer8" ? 8 : 64;
    for (const Y of [0.013, 0.1, 0.27, 0.5, 0.77, 0.95]) {
      let sum = 0;
      for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) sum += levelLinear(plan, quantise(plan, Y, thresholdAt(source, x, y)));
      const k = Math.min(levels - 2, Math.floor(Math.pow(Y, 1 / plan.gamma) * (levels - 1))), step = levelLinear(plan, k + 1) - levelLinear(plan, k);
      assert.ok(Math.abs(sum / (S * S) - Y) <= (0.5 / M) * step + 1e-12, `${levels} ${source} ${Y}: ${sum / (S * S)}`);
    }
  }
});
test("1 bit: two outputs only; tone 0 is never lit and tone 1 is always lit; output is monotone in tone", () => {
  const plan = resolveDitherlight("ink");
  for (let i = 0; i < 64; i++) { const t = thresholdAt("bayer8", i & 7, i >> 3); assert.equal(quantise(plan, 0, t), 0); assert.equal(quantise(plan, 1, t), 1); }
  for (let i = 0; i < 64; i++) { const t = thresholdAt("bayer8", i & 7, i >> 3); let prev = 0; for (let Y = 0; Y <= 1; Y += 0.01) { const k = quantise(plan, Y, t); assert.ok(k >= prev); prev = k; } }
});
test("fog: far surfaces go to exactly paper white whatever the albedo and the light", () => {
  const plan = resolveDitherlight("ink"), px = { kind: 2, albedo: 0.05, depth: 5000, nrm: [0, 1, 0], wpos: [0, 0, 0], vis: [0, 0, 0, 0] };
  assert.equal(toneOf(plan, [], px), 1);
  const t = thresholdAt("blue64", 5, 9); assert.equal(quantise(plan, 1, t), 1);
  assert.deepEqual(levelRgb8(plan, 1), levelRgb8(plan, 1));
});
test("masks: radial and beam are zero outside their reach and within [0, intensity]", () => {
  const r = makeRadial({ pos: [0, 0, 0], radius: 5, core: 1, intensity: 1.2 });
  assert.equal(maskAt(r, [5, 0, 0]), 0); assert.equal(maskAt(r, [9, 0, 0]), 0);
  let prev = Infinity; for (let d = 0.1; d < 5; d += 0.1) { const m = maskAt(r, [d, 0, 0]); assert.ok(m <= prev && m >= 0 && m <= 1.2); prev = m; }
  const b = makeBeam({ pos: [0, 0, 0], dir: [0, 0, -1], outerDeg: 10, innerDeg: 4, range: 20, intensity: 1 });
  assert.equal(maskAt(b, [0, 0, 25]), 0); assert.equal(maskAt(b, [0, 0, 10]), 0);
  assert.ok(maskAt(b, [0, 0, -10]) > 0.4); assert.equal(maskAt(b, [5, 0, -10]), 0);
  assert.equal(maskAt(b, [Math.tan(0.05) * 10, 0, -10]) > 0, true);
  assert.ok(Math.abs(maskAt(b, [1, 0, -10]) - maskAt(b, [-1, 0, -10])) < 1e-12);
  const slat = makeBeam({ pos: [0, 0, 0], dir: [0, 0, -1], outerDeg: 20, innerDeg: 10, range: 20, intensity: 1, slats: 4, duty: 0.3 });
  const vals = new Set(); for (let a = 0; a < 6.28; a += 0.05) vals.add(maskAt(slat, [Math.sin(a) * 1.5, Math.cos(a) * 1.5, -10]) > 0.01);
  assert.equal(vals.size, 2, "blades have gaps");
  assert.equal(surfaceTerm(r, [1, 0, 0], [-1, 0, 0]), 1); assert.equal(surfaceTerm(r, [1, 0, 0], [1, 0, 0]), 0);
});
test("anchoring: screen ignores the surface; world follows it; the octave grows with depth", () => {
  const base = { ix: 3, iy: 4, apos: [1.234, 0.5, 2.5], anrm: [0, 1, 0], depth: 4 }, pa = pixelAngle(1.2, 180);
  const scr = { anchor: "screen" }, wf = { anchor: "world-fixed", cell: 0.1 }, wo = { anchor: "world-octave", cell: 0.1, px: 2, pa };
  assert.deepEqual(anchorCell(scr, { ...base, apos: [9, 9, 9] }), [3, 4]);
  assert.deepEqual(anchorCell(wf, { ...base, ix: 50 }), anchorCell(wf, base));
  assert.deepEqual(anchorCell(wf, base), [12, 25]);
  assert.equal(octaveOf(0.1, 2, pa, 0.1), 0); assert.ok(octaveOf(0.1, 2, pa, 40) > octaveOf(0.1, 2, pa, 10));
  assert.deepEqual(anchorCell(wo, { ...base, depth: 0.01 }), anchorCell(wf, base));
  // the dominant axis picks the projection plane
  assert.deepEqual(anchorCell(wf, { ...base, anrm: [1, 0, 0] }), [25, 5]);
});
test("a rendered frame: motion vectors of a static scene under a camera step reproject to the same surface point", () => {
  const W = 64, H = 36, [s0, s1] = pathSpecs("dolly", 2), g0 = renderFrame(s0, W, H), g1 = renderFrame(s1, W, H);
  let ok = 0, bad = 0;
  for (let i = 0; i < W * H; i++) {
    if (g1.kind[i] !== 2 || !(g1.prev[i * 3] >= 0)) continue;
    const q = Math.round(g1.prev[i * 3 + 1]) * W + Math.round(g1.prev[i * 3]);
    if (q < 0 || q >= W * H || g0.id[q] !== g1.id[i]) continue;
    if (Math.abs(g0.depth[q] - g1.prev[i * 3 + 2]) < 0.05 * g1.prev[i * 3 + 2]) ok++; else bad++;
  }
  assert.ok(ok > 0.3 * W * H, `${ok}`); assert.ok(bad < 0.05 * ok, `${bad}`);
});
test("the demo scene renders deterministically and a full 1-bit frame uses both levels", () => {
  const spec = pathSpecs("static", 1)[0], g = renderFrame(spec, 48, 27), plan = resolveDitherlight("ink", {}, { height: 27 }), a = runDitherlight(plan, lightsOf(spec), g), b = runDitherlight(plan, lightsOf(spec), renderFrame(spec, 48, 27));
  assert.deepEqual(a.level, b.level); assert.ok(a.level.includes(0) && a.level.includes(1));
  const p = project(spec.cam, 48, 27, [0, 1.7, -10]); assert.ok(Math.abs(p[0] - 23.5) < 1.5);
});

test("haze: zero haze changes nothing, full haze is paper white, and it screen-blends monotonically", () => {
  const plan = resolveDitherlight("ink"), base = { kind: 2, albedo: 0.4, depth: 6, nrm: [0, 1, 0], wpos: [0, 0, 0], vis: [0, 0, 0, 0] };
  const t0 = toneOf(plan, [], base);
  assert.equal(toneOf(plan, [], { ...base, haze: 0 }), t0); assert.equal(toneOf(plan, [], { ...base, haze: 1 }), 1);
  let prev = t0; for (let h = 0.05; h <= 1; h += 0.05) { const t = toneOf(plan, [], { ...base, haze: h }); assert.ok(t >= prev - 1e-12 && t <= 1); prev = t; }
  assert.ok(Math.abs(toneOf(plan, [], { ...base, haze: 0.5 }) - (1 - (1 - t0) * 0.5)) < 1e-12);
  // sky and emissive pixels take haze too (a beam shows against the sky)
  assert.equal(toneOf(plan, [], { kind: 0, albedo: 0.2, depth: 40, nrm: [0, 1, 0], wpos: [0, 0, 0], vis: [0, 0, 0, 0], haze: 0.5 }), 0.6);
});
test("the second Salt Road renders deterministically with haze, and haze rises with a light in the ray", async () => {
  const { renderFrame2, lightsOf2 } = await import("../fixtures/dream2.mjs");
  const spec = { cam: { pos: [0.2, 1.1, -2], yaw: 0.15, pitch: 0.12 }, beamAngle: 2.75, flicker: 0, time: 3, spin: 1 }, a = renderFrame2(spec, 40, 24), b = renderFrame2(spec, 40, 24);
  assert.deepEqual(Array.from(a.haze), Array.from(b.haze)); assert.ok(a.haze.every((v) => v >= 0 && v < 1));
  assert.ok(Math.max(...a.haze) > 0.02, "some haze");
  const dark = { ...spec, cam: { pos: [0.2, 1.1, 40], yaw: Math.PI, pitch: 0 } }, d = renderFrame2(dark, 40, 24);
  assert.ok(Math.max(...d.haze) < 0.2 * Math.max(...a.haze), "facing away from every light there is far less haze");
  const plan = resolveDitherlight("ink", {}, { height: 24 }), r = runDitherlight(plan, lightsOf2(spec), a);
  assert.ok(r.level.includes(0) && r.level.includes(1));
});
