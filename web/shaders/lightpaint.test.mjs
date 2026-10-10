// node --test web/shaders/lightpaint.test.mjs
// Pigment-space lighting on its CPU reference. Thresholds from
// evidence/shaders-lightpaint-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader lightpaint.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveLightpaint, shade, runLightpaint, LP_PRESETS } from "./lightpaint/lightpaint.mjs";
import { streetScene } from "./fixtures/street.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-lightpaint-parity-bounds.json", import.meta.url), "utf8")).tests;
const Y = (c) => 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
const cbrt = Math.cbrt;
function oklabHue(c) {
  const l = cbrt(0.4122214708 * c[0] + 0.5363325363 * c[1] + 0.0514459929 * c[2]), m = cbrt(0.2119034982 * c[0] + 0.6806995451 * c[1] + 0.1073969566 * c[2]), s = cbrt(0.0883024619 * c[0] + 0.2817188376 * c[1] + 0.6299787005 * c[2]);
  const a = 1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s, b = 0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s;
  return (Math.atan2(b, a) * 180) / Math.PI;
}
const grid = []; for (const r of [0, 0.25, 0.5, 0.75, 1]) for (const g of [0, 0.25, 0.5, 0.75, 1]) for (const b of [0, 0.25, 0.5, 0.75, 1]) grid.push([r, g, b]);

test("with strength 0 the result is the RGB multiply", () => {
  const plan = resolveLightpaint("disco", { strength: 0 }); let dev = 0;
  for (const a of grid) for (const e of [0.01, 0.2, 1, 5]) { const E = [e, 0.8 * e, 0.6 * e], o = shade(plan, a, E)[0]; for (let k = 0; k < 3; k++) dev = Math.max(dev, Math.abs(o[k] - a[k] * E[k])); }
  assert.ok(dev <= B.strength_zero_equals_multiply_max_dev, `max deviation ${dev}`);
  // Extra check (not a committed bound): the whole street frame comes back at f32 precision.
  const s = streetScene(96, 60, 0), f = runLightpaint(plan, s).data; let fd = 0;
  for (let i = 0; i < f.length; i++) fd = Math.max(fd, Math.abs(f[i] - s.data[i]) / Math.max(1, Math.abs(s.data[i])));
  assert.ok(fd < 1e-5, `frame deviation ${fd}`);
});

test("luminance is monotone in irradiance for every preset (by construction; a guard against regressions)", () => {
  for (const preset of Object.keys(LP_PRESETS)) {
    const plan = resolveLightpaint(preset);
    for (const a of grid) {
      let prev = -Infinity;
      for (let i = 0; i < 64; i++) { const e = 0.002 * Math.pow(1.13, i), y = Y(shade(plan, a, [e, e, e])[0]); assert.ok(y >= prev - B.luminance_monotone_in_irradiance_tolerance, `${preset} ${a} at ${e}: ${y} < ${prev}`); prev = y; }
    }
  }
});

test("yellow in shadow turns toward olive under KM, and keeps its hue under RGB multiply", () => {
  const T = B.yellow_test, plan = resolveLightpaint(T.preset), p = plan.p, e = (p.Emid * T.value) / (1 - T.value);
  const out = shade(plan, T.albedo_linear, [e, e, e])[0], mult = T.albedo_linear.map((v) => v * e);
  // Hue differences wrap to (-180, 180]. The first run lacked the wrap (a test bug: it read +80.9 as
  // -279.1); the same run showed the old disco preset going teal, not olive. Added after that run, a
  // stricter check that the bounds file does not have: the shift must also stay under 40 degrees.
  const wrap = (d) => ((((d + 180) % 360) + 360) % 360) - 180;
  const h0 = oklabHue(T.albedo_linear), dKM = wrap(oklabHue(out) - h0), dRGB = Math.abs(wrap(oklabHue(mult) - h0));
  assert.ok(dKM >= B.yellow_in_shadow_hue_shift_min_deg, `KM hue shift ${dKM} degrees (toward green is positive)`);
  assert.ok(dKM <= 40, `KM hue shift ${dKM} degrees: teal, not olive`);
  assert.ok(dRGB <= B.yellow_rgb_multiply_hue_shift_max_deg, `RGB hue shift ${dRGB}`);
});

test("paint stays a reflectance in [0, 1], and nothing is NaN", () => {
  const [lo, hi] = B.output_range;
  for (const preset of Object.keys(LP_PRESETS)) {
    const plan = resolveLightpaint(preset);
    for (const a of grid) for (const e of [0, 1e-4, 0.05, 0.3, 2, 40]) for (const E of [[e, e, e], [e, 0.72 * e, 0.42 * e], [0.45 * e, 0.6 * e, e]]) {
      const [o, paint] = shade(plan, a, E);
      for (const v of paint) assert.ok(v >= lo && v <= hi, `${preset} paint ${paint}`);
      for (const v of o) assert.ok(Number.isFinite(v), `${preset} out ${o}`);
    }
  }
});
