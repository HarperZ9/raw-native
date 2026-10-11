// node --test web/shaders/fibre.test.mjs
// Fibre-network watercolour on the CPU reference. Thresholds from
// evidence/shaders-fibre-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader fibre.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveFibre, runFibre } from "./fibre/fibre.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-fibre-parity-bounds.json", import.meta.url), "utf8")).tests;
const plate = (w, h, f) => { const d = new Float32Array(w * h * 4); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) d.set([...f(x, y), 1], (y * w + x) * 4); return { width: w, height: h, data: d }; };
// A dark drop on light paper; returns the spread of the drop's pigment (suspended plus deposited).
function spread(over, W = 81, H = 81) {
  // edgeDry 0: the drop is itself an edge, and wetness gating (added after the first run) would hold it.
  const plan = resolveFibre("cold-press", { paper: [1, 1, 1], edgeDry: 0, ...over }, { w: W, h: H });
  const r = runFibre(plan, plate(W, H, (x, y) => (x === 40 && y === 40 ? [0.05, 0.05, 0.05] : [1, 1, 1])), { keep: true });
  let m = 0, sx = 0, sy = 0, vx = 0, vy = 0;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const q = r.P[(y * W + x) * 3] + r.D[(y * W + x) * 3]; m += q; sx += q * x; sy += q * y; }
  sx /= m; sy /= m;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const q = r.P[(y * W + x) * 3] + r.D[(y * W + x) * 3]; vx += q * (x - sx) ** 2; vy += q * (y - sy) ** 2; }
  let kx = 0, ky = 0; for (let i = 0; i < W * H; i++) { kx += r.K[i * 3]; ky += r.K[i * 3 + 2]; }
  return { ratio: Math.sqrt(vx / vy), expect: Math.sqrt(kx / ky) };
}

test("on paper without fibres a drop spreads the same in every direction", () => {
  const s = spread({ perCell: 0, k1: 0, floc: 0 }), r = Math.max(s.ratio, 1 / s.ratio);
  assert.ok(r <= B.isotropic_paper_spread_ratio_max, `spread ratio ${s.ratio}`);
});

test("on paper with aligned fibres a drop elongates as the conductivity tensor says", () => {
  const s = spread({ perCell: 20, md: 0, spread: 0, floc: 0 }), err = Math.abs(s.ratio / s.expect - 1);
  assert.ok(s.expect > 1.5, `the paper must be anisotropic (expected elongation ${s.expect})`);
  assert.ok(err <= B.aligned_fibres_elongation_rel_error_max, `elongation ${s.ratio}, expected ${s.expect}`);
});

test("pigment is conserved", () => {
  const W = 60, H = 40, plan = resolveFibre("rough", {}, { w: W, h: H }), src = plate(W, H, (x, y) => [0.2 + 0.6 * ((x * 7 + y * 3) % 11) / 11, 0.5, 0.9 - 0.5 * (y / H)]);
  const r = runFibre(plan, src, { keep: true }); let before = 0, after = 0;
  for (let i = 0; i < W * H; i++) for (let c = 0; c < 3; c++) { before += plan.p.strength * -Math.log(Math.max(0.02, Math.min(1, src.data[i * 4 + c] / plan.p.paper[c]))); after += r.P[i * 3 + c] + r.D[i * 3 + c]; }
  const err = Math.abs(after - before) / before;
  assert.ok(err <= B.pigment_mass_conservation_rel_error, `relative change ${err}`);
});

const corr = (a, b) => { const n = a.length, ma = a.reduce((s, v) => s + v, 0) / n, mb = b.reduce((s, v) => s + v, 0) / n; let sab = 0, saa = 0, sbb = 0; for (let i = 0; i < n; i++) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) ** 2; sbb += (b[i] - mb) ** 2; } return saa < 1e-18 || sbb < 1e-18 ? 0 : sab / Math.sqrt(saa * sbb); };
test("pigment settles where fibres cross; without flocculation it does not (control)", () => {
  const W = 96, H = 64, src = plate(W, H, () => [0.5, 0.45, 0.4]);
  const run = (floc) => { const r = runFibre(resolveFibre("cold-press", { floc }, { w: W, h: H }), src, { keep: true }); const d = [], chi = []; for (let i = 0; i < W * H; i++) { d.push(r.D[i * 3] + r.D[i * 3 + 1] + r.D[i * 3 + 2]); chi.push(r.F[i * 5 + 4]); } return corr(d, chi); };
  const c = run(3), c0 = run(0);
  assert.ok(c >= B.flocculation_crossing_correlation_min, `correlation ${c}`);
  assert.ok(Math.abs(c0) <= B.control_no_flocculation_correlation_max, `control correlation ${c0}`);
});

test("a uniform wash on paper without fibres dries uniform", () => {
  const W = 40, H = 30, r = runFibre(resolveFibre("hot-press", { perCell: 0 }, { w: W, h: H }), plate(W, H, () => [0.6, 0.3, 0.7]));
  for (let c = 0; c < 3; c++) { let lo = Infinity, hi = -Infinity; for (let i = 0; i < W * H; i++) { lo = Math.min(lo, r.out.data[i * 4 + c]); hi = Math.max(hi, r.out.data[i * 4 + c]); } assert.ok(hi - lo <= B.uniform_wash_no_fibres_max_dev, `channel ${c}: spread ${hi - lo}`); }
});
