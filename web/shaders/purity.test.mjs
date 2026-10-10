// node --test web/shaders/purity.test.mjs
// CRT purity and degauss on the CPU reference. Thresholds from
// evidence/shaders-purity-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader purity.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolvePurity, landing, runPurity, degaussAt, PURITY_PRESETS } from "./purity/purity.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-purity-parity-bounds.json", import.meta.url), "utf8")).tests;
const plate = (w, h, c) => { const d = new Float32Array(w * h * 4); for (let i = 0; i < w * h; i++) d.set([...c, 1], i * 4); return { width: w, height: h, data: d }; };

test("with no stray field the picture is unchanged", () => {
  const plan = resolvePurity("magnetised", { mag: 0, earth: 0, degauss: 0 }), src = plate(64, 48, [0.3, 0.6, 0.9]);
  for (let i = 0; i < src.data.length; i += 7) src.data[i] = (i % 13) / 13;
  const o = runPurity(plan, src, 0).data; let dev = 0;
  for (let i = 0; i < o.length; i++) dev = Math.max(dev, Math.abs(o[i] - src.data[i]));
  assert.ok(dev <= B.zero_field_identity_max_dev, `max deviation ${dev}`);
});

test("light is lost to the black matrix, never created", () => {
  for (const preset of Object.keys(PURITY_PRESETS)) {
    const p = resolvePurity(preset).p;
    for (let e = -1.5; e <= 1.5; e += 0.01) { const f = landing(p, e); for (let k = 0; k < 3; k++) { const s = f[k][0] + f[k][1] + f[k][2]; assert.ok(s <= 1 + 1e-12 && s >= 0, `${preset} e ${e} gun ${k}: ${s}`); } }
  }
});

test("a red beam pushed right lights the green stripe, not the blue", () => {
  const p = resolvePurity("magnetised").p, f = landing(p, 0.12);
  assert.ok(f[0][1] > 0.05 && f[0][2] === 0, `red gun to green ${f[0][1]}, to blue ${f[0][2]}`);
  const g = landing(p, -0.12);
  assert.ok(g[0][2] > 0.05 && g[0][1] === 0, `pushed left, red reaches the previous triad's blue ${g[0][2]}, green ${g[0][1]}`);
});

test("the degauss swing decays with its time constant", () => {
  const p = resolvePurity("degauss").p;
  for (const n of [1, 2, 3]) {
    // Peak of |swing| times e^(t / tau) over one mains cycle starting at n tau, sampled finely.
    let peak = 0; for (let i = 0; i < 2000; i++) { const t = n * p.tau + i / (2000 * p.mains); peak = Math.max(peak, Math.abs(degaussAt(p, t, 0, 1)) * Math.exp(t / p.tau)); }
    const err = Math.abs(peak / p.degauss - 1);
    assert.ok(err <= B.degauss_envelope_rel_error_max, `at ${n} tau: envelope error ${err}`);
  }
  let late = 0; for (let y = 0; y < 240; y++) late = Math.max(late, Math.abs(degaussAt(p, 5 * p.tau, y, 240)) * (0.3 + 1));
  assert.ok(late <= B.after_5_tau_max_dev_from_residual, `landing error left at 5 tau: ${late}`);
});

test("the swing runs down the screen at the mains frequency (lines are drawn at their own times)", () => {
  const p = resolvePurity("degauss").p, H = 480; let crossings = 0, prev = degaussAt(p, 0.05, 0, H);
  for (let y = 1; y < H; y++) { const v = degaussAt(p, 0.05, y, H); if (Math.sign(v) !== Math.sign(prev)) crossings++; prev = v; }
  const want = 2 * p.mains * (1 - p.blank) / p.field;
  assert.ok(Math.abs(crossings - want) <= B.line_phase_zero_crossings_tolerance, `${crossings} zero crossings, about ${want} expected`);
});
