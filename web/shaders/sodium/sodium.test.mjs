// Spectral sodium: the line model, the response table and the metric, each with a control.
import { test } from "node:test";
import assert from "node:assert/strict";
import { sodiumLines, responseTable, paletteSpread, chromaSpread, reflectance, MATERIALS, PALETTE } from "./spectra.mjs";
import { resolveSodium, rainAt } from "./shade.mjs";
import { cmf, XYZ_TO, apply3 } from "../spectral.mjs";

test("sodium lines: D2 twice D1, power sums to 1 with the impurity", () => {
  const L = sodiumLines(0.02), s = L.reduce((a, [, w]) => a + w, 0);
  assert.ok(Math.abs(s - 1) < 1e-12); assert.ok(Math.abs(L[0][1] / L[1][1] - 2) < 1e-12);
});
test("every light's white has luminance 1 and a material never reflects more than white", () => {
  const t = responseTable();
  t.types.forEach((_, L) => { assert.ok(Math.abs(t.xyz[L][MATERIALS.findIndex((m) => m.name === "grey")][1] - 0.5) < 1e-9); for (const row of t.xyz[L]) assert.ok(row[1] <= 1 + 1e-9); });
});
test("pure sodium: every palette colour has the chromaticity of 589 nm, so the spread is ~0", () => {
  const t = responseTable({ impurity: 0 }), c = apply3(XYZ_TO.rec709, cmf(589.2)), w = t.white[0];
  const ratio = (v) => [v[0] / v[1], v[2] / v[1]]; assert.ok(Math.abs(ratio(w)[0] - ratio(c)[0]) < 1e-3);
  assert.ok(paletteSpread(t, { sodium: 1 }) < 1e-3);
  assert.ok(w[2] < 0, "a sodium colour is outside Rec. 709: negative blue in linear light");
});
test("control: a broadband light separates the same palette (the metric is not vacuous)", () => {
  const t = responseTable(); assert.ok(paletteSpread(t, { window: 1 }) > 0.15 && paletteSpread(t, { led: 1 }) > 0.15);
  assert.ok(chromaSpread([[0.3, 0.3, 0.3], [0.6, 0.6, 0.6]]) < 1e-6);
});
test("the palette reflectances stay physical (0 to 1) across the visible band", () => {
  for (let m = 0; m < MATERIALS.length; m++) for (let wl = 380; wl <= 780; wl += 5) { const r = reflectance(m, wl); assert.ok(r >= 0 && r <= 1, `${MATERIALS[m].name} ${wl}`); }
  assert.equal(PALETTE, 8);
});
test("rain streaks are deterministic and sparse", () => {
  let on = 0; for (let x = 0; x < 200; x++) for (let y = 0; y < 100; y++) { const a = rainAt(x, y, 100, 1.3); assert.equal(a, rainAt(x, y, 100, 1.3)); if (a > 0) on++; }
  assert.ok(on > 100 && on < 0.2 * 20000);
  assert.ok(resolveSodium().table === resolveSodium().table, "tables are cached per impurity");
});
