// Lens suite unit tests: the physics constants, energy redistribution, the keyed controls and the
// paraxial against exact ghost trace. Each test fails if its check would be vacuous.
import { test } from "node:test";
import assert from "node:assert/strict";
import { BINS, BIN_RGB, GLASS, abbe, glassIndex } from "./spectrum.mjs";
import { filmReflectance, bare, quarterWaveMinimum } from "./coating.mjs";
import { resolveLens, extractSources, prepareSources, runLens } from "./lens.mjs";
import { lensAtHour, LENS_KEYS } from "./params.mjs";
import { SLOTS_8 } from "../../look/keys.mjs";
import { ghostTable, firstOrder } from "./prescription.mjs";
import { traceExact, chiefRay } from "./exact.mjs";
import { LAMBDA_D } from "./spectrum.mjs";

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b} (tol ${tol})`);

test("bin weights sum to 1 per channel; glass dispersion is plausible", () => {
  for (let k = 0; k < 3; k++) assert.ok(Math.abs(BIN_RGB.reduce((s, w) => s + w[k], 0) - 1) < 1e-12);
  assert.equal(BIN_RGB.length, BINS);
  assert.ok(abbe(GLASS.crown) > 55 && abbe(GLASS.crown) < 70 && abbe(GLASS.flint) > 25 && abbe(GLASS.flint) < 40);
  // The SCHOTT catalogue: N-BK7 nd 1.5168, vd 64.17; N-SF2 nd 1.64769, vd 33.82.
  near(glassIndex(GLASS.crown, 587.5618), 1.5168, 2e-5, "N-BK7 nd"); near(abbe(GLASS.crown), 64.17, 0.02, "N-BK7 vd");
  near(glassIndex(GLASS.flint, 587.5618), 1.64769, 2e-5, "N-SF2 nd"); near(abbe(GLASS.flint), 33.82, 0.02, "N-SF2 vd");
});
test("quarter-wave coating meets its closed form and beats bare glass in the middle of the band", () => {
  assert.ok(Math.abs(filmReflectance(550, 0) - quarterWaveMinimum()) < 1e-9);
  assert.ok(filmReflectance(550, 0) < bare(0) / 3);
  assert.ok(filmReflectance(420, 0) > filmReflectance(550, 0) && filmReflectance(700, 0) > filmReflectance(550, 0));
});
test("the lens redistributes energy: output total equals input within 1%, and the terms add light", () => {
  const W = 257, H = 257, f = { width: W, height: H, data: new Float64Array(W * H * 4) };
  for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) { const i = ((128 + dy) * W + 128 + dx) * 4; f.data[i] = f.data[i + 1] = f.data[i + 2] = 20; }
  const plan = resolveLens({ fNumber: 22, streakLenPx: 14, refHeight: H, caAmount: 0, starCellPxPerStop: 0.08 }, { width: W, height: H }), src = extractSources(plan, f);
  assert.ok(src.list.length > 0, "no source extracted: the check would be vacuous");
  const out = runLens(plan, f, prepareSources(plan, src.list), src.keep);
  const sum = (d, k) => { let s = 0; for (let i = k; i < d.length; i += 4) s += d[i]; return s; };
  let moved = 0; for (let i = 0; i < out.data.length; i += 4) if (Math.abs(out.data[i] - f.data[i]) > 1e-6) moved++;
  assert.ok(moved > 1000, "the lens terms moved almost no light");
  for (let k = 0; k < 3; k++) assert.ok(Math.abs(sum(out.data, k) / sum(f.data, k) - 1) < 0.01, `channel ${k}`);
  const noKeep = runLens(plan, f, prepareSources(plan, src.list), null);
  assert.ok(sum(noKeep.data, 0) / sum(f.data, 0) - 1 > 0.05, "control: without removal the frame must gain light");
});
test("lens controls hit their slot values exactly at each slot hour", () => {
  for (const [s, slot] of SLOTS_8.entries()) {
    const p = lensAtHour(slot.hour);
    for (const [name, spec] of Object.entries(LENS_KEYS)) assert.ok(Math.abs(p[name] - spec.values[s]) < 1e-12, `${name} at ${slot.name}`);
  }
  const mid = lensAtHour(10.7); assert.ok(mid.fNumber >= 8 && mid.fNumber <= 11);
});
test("paraxial ghost centres match an exact meridional trace at 0.2 degrees", () => {
  const gt = ghostTable(undefined, [LAMBDA_D]), { f } = firstOrder(), th = (0.2 * Math.PI) / 180, chief = chiefRay(th);
  assert.equal(gt.ghosts.length, 15);
  for (const g of gt.ghosts) {
    const ex = traceExact({ ...chief }, { i: g.i, j: g.j }), par = g.m[0] * f * Math.tan(th);
    assert.ok(Number.isFinite(ex) && Math.abs(ex - par) < 0.0098, `ghost ${g.i},${g.j}: ${ex} vs ${par}`);
  }
});
