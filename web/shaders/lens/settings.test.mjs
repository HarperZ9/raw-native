// Player lens settings: defaults are the identity (parameters and pixels), slot shape is kept,
// the accessibility preset reduces what the lens adds and still conserves energy, bad input is refused.
import { test } from "node:test";
import assert from "node:assert/strict";
import { lensAtHour } from "./params.mjs";
import { lensForPlayer, applyLensSettings, resolveLensSettings, LENS_SETTINGS_PRESETS, LENS_SETTINGS_DEFAULT } from "./settings.mjs";
import { resolveLens, extractSources, prepareSources, runLens } from "./lens.mjs";
import { lensLightsFrame } from "../fixtures/lens-lights.mjs";
import { SLOTS_8 } from "../../look/keys.mjs";

const hours = [...SLOTS_8.map((s) => s.hour), ...Array.from({ length: 40 }, (_, k) => (k * 24 * 0.6180339887) % 24)];
const render = (params, W = 96, H = 54) => {
  const f = lensLightsFrame(W, H), plan = resolveLens(params, { width: W, height: H }), s = extractSources(plan, f);
  return { f, out: runLens(plan, f, prepareSources(plan, s.list), s.keep), n: s.list.length };
};
const sum = (d) => { let s = 0; for (let i = 0; i < d.length; i++) if ((i & 3) !== 3) s += d[i]; return s; };

test("default settings reproduce the engine parameters exactly at every hour tried", () => {
  for (const h of hours) {
    assert.deepStrictEqual(lensForPlayer(h), lensAtHour(h));
    assert.deepStrictEqual(lensForPlayer(h, "default"), lensAtHour(h));
    assert.deepStrictEqual(lensForPlayer(h, {}), lensAtHour(h));
  }
});
test("default settings reproduce the rendered frame bit for bit", () => {
  for (const h of [0, 12.5, 22]) {
    const a = render(lensAtHour(h)).out.data, b = render(lensForPlayer(h, LENS_SETTINGS_DEFAULT)).out.data;
    assert.equal(a.length, b.length);
    let diff = 0; for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) diff++;
    assert.equal(diff, 0);
  }
  // not vacuous: a non-default setting does change the frame
  const a = render(lensAtHour(22)).out.data, c = render(lensForPlayer(22, { flare: 0.5 })).out.data;
  assert.ok(a.some((v, i) => v !== c[i]));
});
test("settings keep the per-slot shape of each term", () => {
  for (const h of hours) {
    const e = lensAtHour(h), p = lensForPlayer(h, { flare: 0.5, halation: 1.5, streaks: false, fringing: false });
    assert.ok(Math.abs(p.ghostGain - 0.5 * e.ghostGain) < 1e-12 && Math.abs(p.starGain - 0.5 * e.starGain) < 1e-12);
    assert.ok(Math.abs(p.haloGain - 1.5 * e.haloGain) < 1e-12);
    assert.equal(p.streakGain, 0); assert.equal(p.caAmount, 0);
    assert.equal(p.fNumber, e.fNumber); assert.equal(p.threshold, e.threshold);
  }
});
test("reduced-flash adds less light, fewer sources, and still conserves energy", () => {
  const base = render(lensAtHour(22)), red = render(lensForPlayer(22, "reduced-flash"));
  assert.ok(base.n > 0 && red.n > 0 && red.n <= base.n);
  const added = (r) => { let s = 0; const o = r.out.data, f = r.f.data; for (let i = 0; i < o.length; i++) if ((i & 3) !== 3) s += Math.abs(o[i] - f[i]); return s; };
  assert.ok(added(red) < 0.5 * added(base), `${added(red)} vs ${added(base)}`);
  assert.ok(Math.abs(sum(red.out.data) / sum(red.f.data) - 1) < 0.02);
  const p = lensForPlayer(22, "reduced-flash"), e = lensAtHour(22);
  assert.equal(p.threshold, 2 * e.threshold); assert.equal(p.maxSources, Math.ceil(e.maxSources / 2));
});
test("bad settings are refused, not silently fixed", () => {
  assert.throws(() => resolveLensSettings({ bloom: 1 }), /unknown setting/);
  assert.throws(() => resolveLensSettings({ flare: 2 }), /\[0, 1.5\]/);
  assert.throws(() => resolveLensSettings({ streaks: "no" }), /true or false/);
  assert.throws(() => resolveLensSettings("cinema"), /unknown preset/);
  for (const name of Object.keys(LENS_SETTINGS_PRESETS)) assert.doesNotThrow(() => applyLensSettings(lensAtHour(3), name));
});
