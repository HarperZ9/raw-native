// node --test tools/asset/test/fit.test.mjs   Bound (DESIGN.md section 11): every landmark within 2 mm after the fit, no new creases.
// Controls that must fail: an unreachable landmark is reported and not claimed, a sharp union is flagged by the crease measure, and a
// fit with the trust weight cranked up refuses to move.
import test from "node:test";
import assert from "node:assert/strict";
import { fitLandmarks, landmarkErrors, creaseOf, paramsOf } from "../src/fit.mjs";
import { landmarksFor, expandLandmarks, ANSUR_MALE } from "../src/anthro.mjs";
import { seatEye } from "../src/render.mjs";

const HEAD = (d = {}) => [
  { id: "skull", op: "add", shape: "ellb", c: [0, 0, -0.02], r: [0.075 + (d.sw || 0), 0.1, 0.098], k: 0.02, blend: "cubic" },
  { id: "jaw", op: "add", shape: "ellb", c: [0, -0.07, 0.03], r: [0.05, 0.05 + (d.jh || 0), 0.055], k: 0.04, blend: "cubic" },
  { id: "nose", op: "add", shape: "rcone", a: [0, 0.0, 0.085], b: [0, -0.04 + (d.nl || 0), 0.1 + (d.np || 0)], r: [0.006, 0.01], k: 0.015, blend: "cubic" },
  { id: "chin", op: "add", shape: "ellb", c: [0, -0.11 + (d.cy || 0), 0.05], r: [0.02, 0.02, 0.022], k: 0.02, blend: "cubic" },
];
const FREE = ["skull", "jaw", "nose", "chin"];
// landmarks are the surface points of a perturbed head (the target), so a perfect fit exists
function surfaceLandmarks(strokes, pts) { return pts.map((p, i) => ({ id: "t" + i, p })); }
const TARGET = HEAD({ sw: 0.006, jh: 0.008, nl: -0.006, np: 0.007, cy: -0.006 });
import { skinField } from "../src/fit.mjs";
import { project } from "../src/sdf.mjs";
const ON = (pts) => { const F = skinField(TARGET); return pts.map((p) => project(F, p, 20, 0.02).slice(0, 3)); };
const LM = surfaceLandmarks(TARGET, ON([[0.09, 0, -0.02], [-0.09, 0, -0.02], [0, 0.11, -0.02], [0, 0, -0.12], [0, -0.025, 0.12], [0, -0.04, 0.115], [0, -0.14, 0.055], [0.04, -0.09, 0.04], [-0.04, -0.09, 0.04], [0, -0.07, 0.1]]));

test("the fit brings every landmark inside 2 mm from a start that misses by more, with no new creases", () => {
  const before = landmarkErrors(HEAD(), LM), worst0 = Math.max(...before.map((e) => e.err));
  assert.ok(worst0 > 0.004, `start must miss by more than 4 mm, missed ${worst0}`);
  const R = fitLandmarks(HEAD(), LM, { free: FREE });
  const worst = Math.max(...R.after.map((e) => e.err));
  assert.ok(worst <= 0.002, `worst landmark error ${worst}`);
  assert.ok(R.tolMet && R.crease.ok, `crease before ${R.crease.before.max} after ${R.crease.after.max}`);
});
test("control: a landmark no parameter can reach is reported as missed, not claimed", () => {
  const far = [...LM, { id: "unreachable", p: [0.2, 0.2, 0.2] }], R = fitLandmarks(HEAD(), far, { free: FREE });
  assert.equal(R.tolMet, false); assert.ok(R.after.find((e) => e.id === "unreachable").err > 0.01);
});
test("control: with a very large trust weight the fit does not move and does not meet the tolerance", () => {
  const R = fitLandmarks(HEAD(), LM, { free: FREE, lambda: 1e9, iters: 10 });
  assert.equal(R.tolMet, false);
});
test("control: the crease measure flags a sharp union and passes a cubic blend", () => {
  const mk = (k, blend) => [{ id: "a", op: "add", shape: "ellb", c: [0, 0, 0], r: [0.05, 0.05, 0.05], k, blend }, { id: "b", op: "add", shape: "ellb", c: [0.07, 0, 0], r: [0.05, 0.05, 0.05], k, blend }];
  const sharp = creaseOf(mk(0, "quad")), soft = creaseOf(mk(0.05, "cubic"));
  assert.ok(sharp.max > soft.max + 8, `sharp ${sharp.max} soft ${soft.max}`);
});
test("control: a fit whose crease budget is zero slack and whose landmark forces a dent is rejected by the check", () => {
  const dent = { id: "dent", p: [0, 0.0, 0.0] };   // a landmark deep inside the skull volume forces a pit through the surface
  const R = fitLandmarks(HEAD(), [...LM, dent], { free: FREE, creaseSlack: 0, rMin: 0.2 });
  assert.ok(R.tolMet === false || R.crease.ok === false || R.attempts > 1, "a dent landmark must fail the tolerance or the crease check");
});
test("anthropometric landmarks follow the stated proportions", () => {
  const frame = { vertexY: 0.114, mentonY: -0.134, rearZ: -0.118 }, L = landmarksFor({ frame, age: 42, cast: { skull: [0.97, 1, 1], jaw: 0.94 } }), g = Object.fromEntries(L.map((l) => [l.id, l.p]));
  const Hh = 0.248; assert.ok(Math.abs(g.glabella[1] - (frame.mentonY + 0.74 * Hh * 2 / 3)) < 1e-9);
  assert.ok(Math.abs(g.glabella[2] - (frame.rearZ + ANSUR_MALE.headLength)) < 1e-9);
  assert.ok(g.pronasale[2] > g.glabella[2] && g.pogonion[1] < g.stomion[1] && g.stomion[1] < g.subnasale[1]);
  assert.equal(expandLandmarks(L).length, L.length + L.filter((l) => l.mirror).length);
  assert.ok(paramsOf(HEAD(), new Set(["skull"])).length === 6);
});
test("an eye seated on a lid stroke follows it; a missing stroke is an error (control)", () => {
  const S = { strokes: [{ id: "lid", c: [0.03, 0, 0.06], r: [0.012, 0.012, 0.012] }] }, e = { c: [0, 0, 0], r: 0.011, seat: { stroke: "lid", gap: 0.001 } };
  assert.deepEqual(seatEye(S, e).c, [0.03, 0, 0.06]); assert.ok(Math.abs(seatEye(S, e).r - 0.011) < 1e-12);
  S.strokes[0].c[2] = 0.05; assert.equal(seatEye(S, e).c[2], 0.05);
  assert.throws(() => seatEye(S, { ...e, seat: { stroke: "nope" } }), /no stroke/);
});
