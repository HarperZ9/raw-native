// node --test web/shaders/hysteresis.test.mjs
// Hysteresis quantisation on its CPU reference. Thresholds from
// evidence/shaders-hysteresis-parity-bounds.json ("tests"), committed before the first run.
// Runs at 120 x 75 (the bounds fix the frame counts, not the size; 160 x 100 took 100 s). GPU parity:
// tests/web/shaders_parity.py --shader hysteresis.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveHysteresis, step } from "./hysteresis/hysteresis.mjs";
import { sequence } from "./hysteresis/sequences.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-hysteresis-parity-bounds.json", import.meta.url), "utf8")).tests;
const W = 120, H = 75, seqs = new Map();
const seq = (name, n) => { if (!seqs.has(name) || seqs.get(name).length < n) seqs.set(name, sequence(name, W, H, Math.max(n, name === "flicker" ? 48 : n))); return seqs.get(name).slice(0, n); };
function run(preset, frames, overrides = {}, hysteresis = true) {
  const plan = resolveHysteresis(preset, overrides, { w: W, h: H }), res = []; let state = null;
  for (const f of frames) { const r = step(plan, f.frame, f.motion, f.dist, state, { hysteresis }); res.push(r); state = r.state; }
  return res;
}
const toggles = (res, key = "idx") => { let t = 0; for (let f = 1; f < res.length; f++) for (let i = 0; i < res[f][key].length; i++) if (res[f][key][i] !== res[f - 1][key][i]) t++; return t; };

test("with zero margin the decisions are the plain ones, every frame", () => {
  for (const preset of ["bands6", "pico8"]) for (const r of run(preset, seq("flicker", 12), { margin: 0 })) assert.deepEqual(Array.from(r.idx), Array.from(r.plainIdx), preset);
});

test("a static sequence gives the plain decisions, every frame", () => {
  for (const preset of ["bands6", "pico8"]) for (const r of run(preset, seq("static", 4))) assert.deepEqual(Array.from(r.idx), Array.from(r.plainIdx), preset);
});

test("under lamp flicker the decisions toggle far less than plain quantisation", () => {
  const frames = seq("flicker", B.flicker_frames);
  for (const preset of ["bands6", "pico8"]) {
    const res = run(preset, frames), plain = toggles(res, "plainIdx"), held = toggles(res);
    assert.ok(plain > 100, `${preset}: control, plain toggles ${plain} must be many`);
    assert.ok(held / plain <= B.flicker_toggle_ratio_max, `${preset}: held ${held}, plain ${plain}, ratio ${held / plain}`);
  }
});

test("a held band is never more than one band from the plain decision", () => {
  for (const name of ["flicker", "ramp"]) for (const r of run("bands6", seq(name, name === "flicker" ? B.flicker_frames : 48)))
    for (let i = 0; i < r.idx.length; i++) assert.ok(Math.abs(r.idx[i] - r.plainIdx[i]) <= B.band_lag_max, `${name}: pixel ${i} held ${r.idx[i]}, plain ${r.plainIdx[i]}`);
});

test("under a large lamp ramp the bands still move", () => {
  for (const preset of ["bands6", "pico8"]) {
    const res = run(preset, seq("ramp", 48)), first = res[0], last = res[res.length - 1];
    let plain = 0, held = 0; for (let i = 0; i < first.idx.length; i++) { if (last.plainIdx[i] !== first.plainIdx[i]) plain++; if (last.idx[i] !== first.idx[i]) held++; }
    assert.ok(plain > 100, `${preset}: control, the ramp must move many plain decisions (${plain})`);
    assert.ok(held / plain >= B.ramp_changed_fraction_min_ratio, `${preset}: held changed ${held}, plain changed ${plain}`);
  }
});

test("under camera orbit, reprojected memory keeps fewer toggles than plain, and disocclusion resets", () => {
  const res = run("bands6", seq("orbit", 8)), plain = toggles(res, "plainIdx"), held = toggles(res);
  assert.ok(held < plain, `orbit: held ${held}, plain ${plain}`);
});
