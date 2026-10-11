// The post stack's encoding contract (post.mjs, planScenePasses), held by
// evidence/motion-colour-pixel-bounds.json: film reads scene-linear light and writes display
// values, so on display-encoded colours the stack decodes sRGB first, and a list that would
// tone-map twice is refused before anything renders.
import { test } from "node:test";
import assert from "node:assert/strict";
import { planScenePasses, getPass } from "./post.mjs";
import "../shaders/post.mjs";

const names = (l) => l.map((s) => s.pass);

test("film on display-encoded colours gets the sRGB decode first", () => {
  assert.deepEqual(names(planScenePasses([{ pass: "film" }], false)), ["srgb-decode", "film"]);
});
test("film on scene-linear light with post.colour is refused: two tone mappers", () => {
  assert.throws(() => planScenePasses([{ pass: "film" }], true), /must not set post.colour/);
});
test("a linear-input pass after a display-output pass is refused", () => {
  assert.throws(() => planScenePasses([{ pass: "film" }, { pass: "film" }], false), /writes display values/);
});
test("passes that take any input are left alone, and display-stage passes are not scene passes", () => {
  assert.deepEqual(names(planScenePasses([{ pass: "exposure", ev: 1 }, { pass: "dither" }], false)), ["exposure"]);
  assert.deepEqual(names(planScenePasses([{ pass: "exposure", ev: 1 }], true)), ["exposure"]);
});
test("the decode is the IEC 61966-2-1 EOTF", () => {
  const cpu = getPass("srgb-decode").cpu, out = cpu(Float32Array.from([0, 0.04045, 0.5, 1, 0.25, -0.5, 1, 1]));
  assert.equal(out[0], 0);
  assert.ok(Math.abs(out[1] - 0.04045 / 12.92) < 1e-7);
  assert.ok(Math.abs(out[2] - 0.21404114) < 1e-6);
  assert.ok(Math.abs(out[3] - 1) < 1e-6);
  assert.equal(out[3 + 0], out[3]);
  assert.ok(Math.abs(out[5] + 0.21404114) < 1e-6, "sign-symmetric");
  assert.equal(out[7], 1, "alpha unchanged");
});
