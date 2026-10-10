// node --test web/shaders/glaze.test.mjs
// Interference glazes on the CPU reference. Thresholds from
// evidence/shaders-glaze-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader glaze.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveGlaze, filmReflectance, glazeColour, bodySpectrum, GLAZE_PRESETS } from "./glaze/glaze.mjs";
import { latentOf } from "./lightpaint/lightpaint.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-glaze-parity-bounds.json", import.meta.url), "utf8")).tests;
const T = B.peak_test, plan = resolveGlaze("beetle", { n0: T.medium_n, n2: T.substrate_n, filmN: T.film_n, dispersion: T.dispersion });

// Peak of R(lambda) by golden-section search on the continuous function, in [450, 700] nm.
function peak(cosAir) {
  let a = 450, b = 700; const g = (Math.sqrt(5) - 1) / 2, f = (l) => -filmReflectance(plan.p, l, cosAir, T.thickness_nm);
  let c = b - g * (b - a), d = a + g * (b - a);
  for (let i = 0; i < 80; i++) { if (f(c) < f(d)) b = d; else a = c; c = b - g * (b - a); d = a + g * (b - a); }
  return (a + b) / 2;
}

test("the normal-incidence peak sits at 4 n d (first order)", () => {
  const want = 4 * T.film_n * T.thickness_nm, got = peak(1);
  assert.ok(Math.abs(got - want) <= B.normal_incidence_peak_nm_tolerance, `peak ${got} nm, want ${want}`);
});

test("at 45 degrees the peak moves to 4 d sqrt(n^2 - sin^2) (toward blue)", () => {
  const c = Math.cos(Math.PI / 4), want = 4 * T.thickness_nm * Math.sqrt(T.film_n ** 2 - (1 - c * c)), got = peak(c);
  assert.ok(Math.abs(got - want) <= B.angle_45_peak_nm_tolerance, `peak ${got} nm, want ${want}`);
  assert.ok(got < peak(1), "the peak must move toward blue");
});

const grid = []; for (const r of [0, 0.3, 0.7, 1]) for (const g of [0, 0.3, 0.7, 1]) for (const b of [0, 0.3, 0.7, 1]) grid.push([r, g, b]);

test("with no flakes the glaze gives back the body colour", () => {
  let dev = 0;
  for (const preset of Object.keys(GLAZE_PRESETS)) { const p = resolveGlaze(preset, { coverage: 0 }).p; for (const a of grid) for (const cs of [1, 0.5, 0.1]) { const o = glazeColour(p, a, cs, 200); for (let k = 0; k < 3; k++) dev = Math.max(dev, Math.abs(o[k] - a[k])); } }
  assert.ok(dev <= B.zero_coverage_equals_body_max_dev, `max deviation ${dev}`);
});

test("a film of zero thickness is nearly invisible (only the binder-to-mica Fresnel step)", () => {
  let dev = 0;
  for (const preset of Object.keys(GLAZE_PRESETS)) { const p = resolveGlaze(preset).p; for (const a of grid) for (const cs of [1, 0.7, 0.4]) { const o = glazeColour(p, a, cs, 0); for (let k = 0; k < 3; k++) dev = Math.max(dev, Math.abs(o[k] - a[k])); } }
  assert.ok(dev <= B.zero_thickness_max_dev_from_body, `max deviation ${dev}`);
});

test("total spectral reflectance never exceeds 1", () => {
  let worst = 0;
  for (const preset of Object.keys(GLAZE_PRESETS)) {
    const p = resolveGlaze(preset, { coverage: 1 }).p;
    for (const a of grid) { const Rb = bodySpectrum(latentOf(a).c); for (const cs of [1, 0.6, 0.2, 0.02]) for (const d of [0, 60, 150, 300, 600]) for (let w = 0; w < Rb.length; w++) {
      const Rf = filmReflectance(p, 400 + 10 * w, cs, d), Rt = Rf + ((1 - Rf) ** 2 * Rb[w]) / (1 - Rf * Rb[w]); worst = Math.max(worst, Rt);
    } }
  }
  assert.ok(worst <= B.reflectance_max, `max reflectance ${worst}`);
});
