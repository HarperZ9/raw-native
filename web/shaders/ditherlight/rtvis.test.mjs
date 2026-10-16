// RT visibility through the threshold: the net property (Q0 of the protocol), the disc map and a
// visibility sanity check on the analytic yard.
import { test } from "node:test";
import assert from "node:assert/strict";
import { bitrev6, pascal6, concentric, visibility } from "./rtvis.mjs";
import { BLOCKERS, occludedBy, yardLight } from "../fixtures/penumbra.mjs";

test("the 64 rank points form a (0, 6, 3)-net in base 2: every elementary box holds exactly one", () => {
  let shapes = 0;
  for (let a = 0; a <= 6; a++) for (let b = 0; a + b <= 6; b++) {
    const c = 6 - a - b, seen = new Set();
    for (let r = 0; r < 64; r++) seen.add(`${r >> (6 - a)},${bitrev6(r) >> (6 - b)},${pascal6(r) >> (6 - c)}`);
    assert.equal(seen.size, 64, `box shape ${a},${b},${c}`); shapes++;
  }
  assert.equal(shapes, 28);
});
test("a non-net control fails the same check (the check is not vacuous)", () => {
  let bad = 0;
  for (let a = 0; a <= 6; a++) for (let b = 0; a + b <= 6; b++) {
    const c = 6 - a - b, seen = new Set();
    for (let r = 0; r < 64; r++) seen.add(`${r >> (6 - a)},${(r & 7) * 8 >> (6 - b)},${(r >> 3) * 8 >> (6 - c)}`);
    if (seen.size !== 64) bad++;
  }
  assert.ok(bad > 0);
});
test("the concentric map stays in the unit disc and covers it evenly", () => {
  let inside = 0, n = 0, sumR2 = 0;
  for (let j = 0; j < 64; j++) for (let i = 0; i < 64; i++) { const [x, y] = concentric((i + 0.5) / 64, (j + 0.5) / 64); n++; if (x * x + y * y <= 1 + 1e-12) inside++; sumR2 += x * x + y * y; }
  assert.equal(inside, n); assert.ok(Math.abs(sumR2 / n - 0.5) < 0.01); // E[r^2] = 1/2 for a uniform disc
});
test("visibility is 1 in the open, 0 deep in the wall's shadow, fractional in the penumbra", () => {
  const L = yardLight(), occ = occludedBy(BLOCKERS["wall-x"]);
  assert.equal(visibility("ref64", L, [0, 0, -2], 0, 0, 0, occ), 1);
  assert.equal(visibility("ref64", L, [0, 0, 1.0], 0, 0, 0, occ), 0);
  const v = visibility("truth", L, [0, 0, 1.73], 0, 0, 0, occ); assert.ok(v > 0.05 && v < 0.95, `penumbra V ${v}`);
});
