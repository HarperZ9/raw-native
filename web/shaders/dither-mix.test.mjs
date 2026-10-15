// The mix dither (web/shaders/dither/mix.mjs), held to evidence/shaders-dither-mix-bounds.json.
import test from "node:test";
import assert from "node:assert/strict";
import { ditherRef } from "./dither/dither.mjs";
import { mixPalette, mixPlan, PLAN } from "./dither/mix.mjs";
import { labPalette, SRGB8_TO_LINEAR } from "./reference/retro-palettes.mjs";

test("a mode must be named: no default", () => {
  assert.throws(() => ditherRef(new Uint8Array(4), 1, 1, { palette: "ega" }), /name a mode/);
  assert.throws(() => ditherRef(new Uint8Array(4), 1, 1, { palette: "ega", mode: "mix-bayer4", brightness: 0.1 }), /brightness 0 only/);
});
test("a plan is sorted by lightness and its linear mean is close to the colour", () => {
  const pal = labPalette("ega"), mp = mixPalette(pal);
  for (const s of [[30, 28, 25], [20, 56, 51], [200, 120, 40], [128, 128, 128], [5, 5, 8]]) {
    const t = s.map((v) => SRGB8_TO_LINEAR[v]), plan = mixPlan(...t, mp);
    assert.equal(plan.length, PLAN);
    for (let i = 1; i < PLAN; i++) assert.ok(pal[plan[i - 1]].lab[0] <= pal[plan[i]].lab[0], `sorted ${s}`);
    const m = [0, 1, 2].map((c) => plan.reduce((a, k) => a + mp.lin[k][c], 0) / PLAN);
    // Within one sixteenth of the widest palette step in each channel.
    m.forEach((v, c) => assert.ok(Math.abs(v - t[c]) < 1 / PLAN, `${s} channel ${c}: mean ${v} vs ${t[c]}`));
  }
});
test("a flat field under mix-bayer4 uses each plan entry once per 4 x 4 tile", () => {
  const w = 8, h = 8, b = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) b.set([20, 56, 51, 255], i * 4);
  const idx = ditherRef(b, w, h, { palette: "ega", mode: "mix-bayer4" }), plan = mixPlan(...[20, 56, 51].map((v) => SRGB8_TO_LINEAR[v]), mixPalette(labPalette("ega")));
  const tile = []; for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) tile.push(idx[y * w + x]);
  assert.deepEqual(tile.sort((a, c) => a - c), plan.slice().sort((a, c) => a - c));
});
// Shader lab 4 (2026-10-14) reported mix-bayer8 as identical to mix-bayer4 by construction. That
// holds at strength 1 only: the plan index is floor(16 t), and an 8 x 8 Bayer value v8 = 4 v4 + k
// gives floor(16 (v8 + 0.5) / 64) = v4. Below strength 1 the threshold is folded toward 0.5 before
// the floor, the extra Bayer 8 levels survive, and the two modes differ. So mix-bayer8 stays a mode
// of its own, not an alias.
test("mix-bayer8 equals mix-bayer4 at strength 1 and differs below it", () => {
  let seed = 12345; const r = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296;
  const w = 64, h = 48, diff = (a, b) => a.reduce((n, v, i) => n + (v !== b[i]), 0);
  for (const palette of ["pico8", "ega", "gameboy", "cga"]) {
    const b = new Uint8Array(w * h * 4);
    for (let i = 0; i < b.length; i++) b[i] = (i & 3) === 3 ? 255 : Math.floor(r() * 256);
    const run = (mode, strength) => ditherRef(b, w, h, { palette, mode, strength });
    assert.equal(diff(run("mix-bayer8", 1), run("mix-bayer4", 1)), 0, `${palette}: strength 1, identical`);
    assert.ok(diff(run("mix-bayer8", 0.9), run("mix-bayer4", 0.9)) > 0, `${palette}: strength 0.9, the modes differ`);
  }
});
