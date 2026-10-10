// node --test web/shaders/sag.test.mjs
// Scan-causal EHT sag on its CPU reference. Thresholds from evidence/shaders-sag-parity-bounds.json
// ("tests"), committed before the first run. GPU parity: tests/web/shaders_parity.py --shader sag.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveSag, lineCurrents, scan, runSag } from "./sag/sag.mjs";
import { labSource } from "./lab/sources.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-sag-parity-bounds.json", import.meta.url), "utf8")).tests;
const field = (w, h, f) => { const d = new Float32Array(w * h * 4); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const v = f(x, y); d.set([v, v, v, 1], (y * w + x) * 4); } return { width: w, height: h, data: d }; };
const W = 160, H = 240;

test("zero beam current gives the identity raster", () => {
  const plan = resolveSag("thriller", {}, { w: W, h: H }), sc = scan(plan, lineCurrents(plan, field(W, H, () => 0)));
  let dev = 0; for (let y = 0; y < H; y++) dev = Math.max(dev, Math.abs(sc.Y[y] - (y + 0.5)), Math.abs(sc.k[y] - 1));
  assert.ok(dev <= B.black_frame_identity_max_dev, `max deviation ${dev}`);
});

test("a bright box moves no line above it (causality)", () => {
  // The same frame with and without the box, through the same tube.
  const top = 100, box = field(W, H, (x, y) => (y >= top && y < 140 && x > 40 && x < 120 ? 1 : 0.3)), bare = field(W, H, () => 0.3);
  const plan = resolveSag("thriller", { settle: false }, { w: W, h: H });
  const a = runSag(plan, box).out.data, b = runSag(plan, bare).out.data;
  // Output rows whose two source lines both lie above the box, and whose row centre lies above the
  // box's first drawn line.
  const sc = runSag(plan, box).sc; let dev = 0, rows = 0;
  for (let yo = 0; yo < H; yo++) { if (yo + 1.5 >= sc.Y[top - 1]) break; rows++; for (let x = 0; x < W * 4; x++) dev = Math.max(dev, Math.abs(a[yo * W * 4 + x] - b[yo * W * 4 + x])); }
  assert.ok(rows > 80, `only ${rows} rows checked`);
  assert.ok(dev <= B.lines_above_box_max_dev, `rows above the box changed by ${dev}`);
  // Control (revised after the first run: row 200 lies 60 lines below the box, where the sag has
  // decayed to e^-4.6 and a uniform grey cannot show a shift; the band just below the box can).
  let below = 0; for (let i = 140 * W * 4; i < 170 * W * 4; i++) below = Math.max(below, Math.abs(a[i] - b[i]));
  assert.ok(below > 1e-3, `control: a row below the box must change (changed by ${below})`);
});

test("a white field settles at the steady-state gain 1 / sqrt(1 - S)", () => {
  const plan = resolveSag("thriller", { tauFrame: 0.01 }, { w: W, h: H }), sc = scan(plan, lineCurrents(plan, field(W, H, () => 1)));
  const want = 1 / Math.sqrt(1 - plan.p.S), err = Math.abs(sc.k[H - 1] - want) / want;
  assert.ok(err <= B.steady_state_scale_max_rel_error, `relative error ${err}`);
});

test("recovery after a bright band takes tau lines, and twice as long at twice tau", () => {
  const rec = (tauFrame) => {
    const plan = resolveSag("consumer", { tauFrame, settle: false }, { w: W, h: H }), band = field(W, H, (x, y) => (y >= 40 && y < 60 ? 1 : 0));
    const sc = scan(plan, lineCurrents(plan, band)), v0 = sc.v[59], target = v0 / Math.E;
    for (let y = 60; y < H - 1; y++) if (sc.v[y] <= target) { const t = y - 1 + (sc.v[y - 1] - target) / (sc.v[y - 1] - sc.v[y]); return { lines: t - 59, tau: plan.tauLines }; }
    return { lines: NaN, tau: plan.tauLines };
  };
  const a = rec(0.1), b = rec(0.2);
  assert.ok(Math.abs(a.lines - a.tau) <= B.recovery_lines_tolerance, `recovered in ${a.lines} lines, tau ${a.tau}`);
  const r = b.lines / a.lines, [lo, hi] = B.recovery_ratio_for_2x_tau;
  assert.ok(r >= lo && r <= hi, `ratio ${r}`);
});

test("line positions stay strictly increasing on every case frame", () => {
  for (const preset of ["consumer", "thriller"]) for (const name of ["street-signal", "game", "box"]) {
    const src = name === "game" ? labSource("game", 256, 224, 1) : labSource(name, 320, 240), plan = resolveSag(preset, {}, { w: src.width, h: src.height });
    const sc = scan(plan, lineCurrents(plan, src));
    for (let y = 1; y < src.height; y++) assert.ok(sc.Y[y] > sc.Y[y - 1], `${preset}/${name}: line ${y} at ${sc.Y[y]} not below ${sc.Y[y - 1]}`);
  }
});
