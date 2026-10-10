// node --test web/shaders/caricature.test.mjs
// Salience caricature warp on the CPU reference. Thresholds from
// evidence/shaders-caricature-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader caricature.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveCaricature, runCaricature, salienceFrom, jacobianA, gradAt } from "./caricature/caricature.mjs";
import { streetScene } from "./fixtures/street.mjs";
import { hash3 } from "./common.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-caricature-parity-bounds.json", import.meta.url), "utf8")).tests;
const W = 160, H = 100, street = streetScene(W, H, 0);
const detMin = (plan, r) => { let m = Infinity; for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const A = jacobianA(plan, r.g, x, y), l = r.lambda; m = Math.min(m, (1 + l * A[0]) * (1 + l * A[3]) - l * A[1] * l * A[2]); } return m; };

test("with no salience the frame is unchanged", () => {
  const plan = resolveCaricature("grotesque", {}, { w: W, h: H }), r = runCaricature(plan, street, new Float64Array(W * H));
  let dev = 0; for (let i = 0; i < r.out.data.length; i++) if ((i & 3) !== 3) dev = Math.max(dev, Math.abs(r.out.data[i] - street.data[i]));
  assert.ok(dev <= B.zero_salience_identity_max_dev, `max deviation ${dev}`);
});

test("the warp never folds, for random salience and a huge lambda", () => {
  for (let seed = 1; seed <= 6; seed++) {
    const plan = resolveCaricature("grotesque", { lambda: 100, sigma: 0.02 + 0.02 * (seed % 3) }, { w: W, h: H });
    const sal = Float64Array.from({ length: W * H }, (_, i) => hash3(i % W, Math.floor(i / W), seed) ** 3 * 5);
    const r = runCaricature(plan, street, sal), m = detMin(plan, r);
    assert.ok(m >= B.jacobian_determinant_min, `seed ${seed}: min det ${m}`);
  }
});

test("the figure's head is magnified", () => {
  const plan = resolveCaricature("grotesque", {}, { w: W, h: H }), r = runCaricature(plan, street, salienceFrom(plan, street.mat));
  let best = -1, bx = 0, by = 0; for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (r.S[y * W + x] > best) { best = r.S[y * W + x]; bx = x; by = y; }
  const A = jacobianA(plan, r.g, bx, by), l = r.lambda, det = (1 + l * A[0]) * (1 + l * A[3]) - l * A[1] * l * A[2], mag = 1 / det;
  assert.ok(street.mat[by * W + bx] === 5 || street.mat[by * W + bx] === 6, `the salience peak lies on material ${street.mat[by * W + bx]}`);
  assert.ok(mag >= B.magnification_at_salience_peak_min, `magnification ${mag} at (${bx}, ${by})`);
});

// Added with the composed steps: the bound applies to the whole map, so check the composed map's own
// discrete Jacobian (central differences of the final source positions), on the street and on
// random salience.
test("the composed warp never folds either", () => {
  const cases = [["street", null], ...[1, 2, 3].map((seed) => ["random " + seed, seed])];
  for (const [name, seed] of cases) {
    const plan = resolveCaricature("grotesque", seed ? { sigma: 0.02 + 0.02 * (seed % 3) } : {}, { w: W, h: H });
    const sal = seed ? Float64Array.from({ length: W * H }, (_, i) => hash3(i % W, Math.floor(i / W), seed) ** 3 * 5) : salienceFrom(plan, street.mat);
    const r = runCaricature(plan, street, sal), steps = plan.p.steps || 1, pos = new Float64Array(W * H * 2);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const i = y * W + x; let fx = x + 0.5 + r.lambda * r.g[i * 2], fy = y + 0.5 + r.lambda * r.g[i * 2 + 1]; for (let k = 1; k < steps; k++) { const d = gradAt(plan, r.g, fx, fy); fx += r.lambda * d[0]; fy += r.lambda * d[1]; } pos[i * 2] = fx; pos[i * 2 + 1] = fy; }
    let m = Infinity;
    for (let y = 1; y < H - 1; y++) for (let x = 1; x < W - 1; x++) { const P = (xx, yy, c) => pos[(yy * W + xx) * 2 + c]; const a = 0.5 * (P(x + 1, y, 0) - P(x - 1, y, 0)), b = 0.5 * (P(x, y + 1, 0) - P(x, y - 1, 0)), c = 0.5 * (P(x + 1, y, 1) - P(x - 1, y, 1)), d = 0.5 * (P(x, y + 1, 1) - P(x, y - 1, 1)); m = Math.min(m, a * d - b * c); }
    assert.ok(m >= B.jacobian_determinant_min, `${name}: composed min det ${m}`);
  }
});
