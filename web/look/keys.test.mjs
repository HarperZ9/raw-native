import test from "node:test";
import assert from "node:assert/strict";
import { createLookKeys, resolveSlots, SLOTS_6, SLOTS_8, locate } from "./keys.mjs";

const exposure8 = [0.2, 0.15, 0.6, 1.0, 1.2, 0.9, 0.5, 0.25];
const make = (slots = 8, vals = exposure8) => createLookKeys({ slots, params: { exposure: vals, tint: vals.map((v) => [v, 1 - v / 2, 0.5]) } });

test("eight slots by default, six accepted, fewer than six and bad hours refused", () => {
  assert.equal(resolveSlots().length, 8); assert.equal(resolveSlots(8), SLOTS_8); assert.equal(resolveSlots(6), SLOTS_6);
  for (const n of [0, 1, 3, 5]) assert.throws(() => resolveSlots(Array.from({ length: n }, (_, i) => ({ name: `s${i}`, hour: i * 4 }))), /too few/);
  assert.throws(() => resolveSlots(3), /8, 6 or an array/);
  assert.throws(() => resolveSlots(SLOTS_8.map((s, i) => ({ ...s, hour: i === 3 ? 1 : s.hour }))), /increase/);
  assert.equal(resolveSlots(Array.from({ length: 7 }, (_, i) => ({ name: `s${i}`, hour: i * 3 }))).length, 7);
  assert.throws(() => createLookKeys({ slots: 8, params: { x: [1, 2, 3, 4, 5, 6] } }), /needs 8 values/);
  assert.equal(make(6, [0.2, 0.6, 1.0, 1.2, 0.5, 0.25]).slots.length, 6);
});
test("the curve passes through every key exactly, scalar and vector", () => {
  const k = make();
  SLOTS_8.forEach((s, i) => { const v = k.at(s.hour); assert.equal(v.exposure, exposure8[i]); assert.deepEqual(v.tint, [exposure8[i], 1 - exposure8[i] / 2, 0.5]); });
});
test("no overshoot: between two slots the value stays inside the two keys, every hour, both slot counts", () => {
  for (const [slots, vals] of [[8, exposure8], [6, [0.2, 0.6, 1.0, 1.2, 0.5, 0.25]], [8, [0, 0, 1, 1, 0, 0, 1, 0]]]) {
    const k = make(slots, vals), S = k.slots;
    for (let h = 0; h < 24; h += 0.05) {
      const { i, j } = locate(S, h), v = k.at(h).exposure, lo = Math.min(vals[i], vals[j]), hi = Math.max(vals[i], vals[j]);
      assert.ok(v >= lo - 1e-12 && v <= hi + 1e-12, `${slots} slots, hour ${h.toFixed(2)}: ${v} outside [${lo}, ${hi}]`);
    }
  }
});
test("the day wraps: continuous across midnight, and hour 24 equals hour 0", () => {
  const k = make();
  assert.equal(k.at(24).exposure, k.at(0).exposure); assert.equal(k.at(-2).exposure, k.at(22).exposure);
  assert.ok(Math.abs(k.at(23.999).exposure - k.at(0.001).exposure) < 0.02);
  // between the last slot (22) and the first (0 = 24) the value moves from 0.25 to 0.2
  const v = k.at(23).exposure; assert.ok(v <= 0.25 + 1e-12 && v >= 0.2 - 1e-12);
});
test("monotone data stays monotone", () => {
  const k = createLookKeys({ slots: 8, params: { rise: [0, 1, 2, 3, 4, 5, 6, 7] } });
  let prev = -Infinity; for (let h = 0; h <= 22; h += 0.1) { const v = k.at(h).rise; assert.ok(v >= prev - 1e-12); prev = v; }
});
test("ranges clamp, unknown terms are refused, and a clamp never moves a key inside its range", () => {
  const k = createLookKeys({ slots: 8, params: { e: { values: exposure8, min: 0.18, max: 1.1 } } });
  for (let h = 0; h < 24; h += 0.1) { const v = k.at(h).e; assert.ok(v >= 0.18 && v <= 1.1); }
  assert.equal(k.at(12.5).e, 1.1); assert.equal(k.at(0).e, 0.2);
  assert.throws(() => k.term("nope", 3), /unknown term/);
});
