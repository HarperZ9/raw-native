// node --test web/anim/anim.test.mjs
// Skeletons, linear-blend skinning, phase-timed clips, the time warp and two-bone IK.
import { test } from "node:test";
import assert from "node:assert/strict";
import { makeSkeleton, poseSkeleton, skinMatrices, skin, rigidInverse, quatAxisAngle, quatEuler, jointPos } from "./skeleton.mjs";
import { layout, keyTime, monotoneCubic, sampleClip, phaseAt, warp } from "./clip.mjs";
import { twoBone, rotationBetween } from "./ik.mjs";
import { mul, apply } from "../world/scene.mjs";

const near = (a, b, e = 1e-9, msg = "") => assert.ok(Math.abs(a - b) <= e, `${msg} ${a} != ${b}`);
const near3 = (a, b, e = 1e-9, msg = "") => { for (let k = 0; k < 3; k++) near(a[k], b[k], e, msg); };
const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
const qrot = (q, v) => { const [x, y, z, w] = q, u = [x, y, z], c1 = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]], c2 = [u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0]]; return [v[0] + 2 * (w * c1[0] + c2[0]), v[1] + 2 * (w * c1[1] + c2[1]), v[2] + 2 * (w * c1[2] + c2[2])]; };

const leg = () => makeSkeleton([{ name: "hip", t: [0, 1, 0] }, { name: "knee", parent: "hip", t: [0, -0.5, 0] }, { name: "ankle", parent: "knee", t: [0, -0.45, 0] }]);
// a small mesh: one vertex per joint segment plus blended ones, with normals
const mesh = () => ({
  pos: new Float32Array([0.05, 0.8, 0, 0.05, 0.3, 0, 0.05, 0.5, 0, 0.1, 0.75, 0.02]),
  nrm: new Float32Array([1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1]),
  joints: new Uint16Array([0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 2, 0]),
  weights: new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 0.5, 0.5, 0, 0, 0.2, 0.3, 0.5, 0]),
});

test("rest pose: joints sit at their summed offsets, the rigid inverse inverts", () => {
  const sk = leg(), w = poseSkeleton(sk, {});
  near3(jointPos(w, 2), [0, 0.05, 0], 1e-12, "ankle");
  const m = mul(w[1], rigidInverse(w[1]));
  for (let i = 0; i < 16; i++) near(m[i], i % 5 === 0 ? 1 : 0, 1e-12, `identity[${i}]`);
});

test("a joint must be defined after its parent", () => {
  assert.throws(() => makeSkeleton([{ name: "knee", parent: "hip" }, { name: "hip" }]), /before it is defined/);
  assert.throws(() => makeSkeleton([{ name: "a" }, { name: "a" }]), /duplicate/);
});

test("skinning the bind pose returns the mesh unchanged", () => {
  const sk = leg(), M = skinMatrices(poseSkeleton(sk, {}), sk.invBind), m = mesh(), out = skin(m, M);
  for (let i = 0; i < m.pos.length; i++) near(out.pos[i], m.pos[i], 1e-6, `pos[${i}]`);
  for (let i = 0; i < m.nrm.length; i++) near(out.nrm[i], m.nrm[i], 1e-6, `nrm[${i}]`);
});

test("a vertex bound to one joint follows that joint exactly; a half-and-half vertex sits midway", () => {
  const sk = leg(), pose = { knee: { r: quatAxisAngle([1, 0, 0], 1.1) }, hip: { r: quatEuler(0.3, -0.4, 0.1), t: [0.2, 0, -0.1] } };
  const W = poseSkeleton(sk, pose), M = skinMatrices(W, sk.invBind), m = mesh(), out = skin(m, M);
  const rigid = (j, v) => apply(M[j], v);
  near3([out.pos[3], out.pos[4], out.pos[5]], rigid(1, [0.05, 0.3, 0]), 1e-5, "knee-bound vertex");
  const a = rigid(0, [0.05, 0.5, 0]), b = rigid(1, [0.05, 0.5, 0]);
  near3([out.pos[6], out.pos[7], out.pos[8]], [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], 1e-5, "blended vertex");
  for (let v = 0; v < 4; v++) near(Math.hypot(out.nrm[v * 3], out.nrm[v * 3 + 1], out.nrm[v * 3 + 2]), 1, 1e-6, "unit normal");
  // the bone keeps its length under any pose
  near(dist(jointPos(W, 1), jointPos(W, 2)), 0.45, 1e-12, "shin length");
});

test("layout: retiming a phase moves the keys after it and never re-keys", () => {
  const clip = { phases: ["startup", "active", "recovery"], keys: [{ at: ["startup", 0], value: { a: 0 } }, { at: ["active", 0], value: { a: 1 } }, { at: ["recovery", 1], value: { a: 0 } }] };
  const L1 = layout(clip, { startup: 7, active: 3, recovery: 12 }), L2 = layout(clip, { startup: 14, active: 3, recovery: 12 });
  assert.equal(L1.length, 22); assert.equal(L2.length, 29);
  assert.equal(keyTime(["active", 0], L1), 7); assert.equal(keyTime(["active", 0], L2), 14);
  near(sampleClip(clip, L2, 14).a, 1, 1e-12, "the active key lands on the new startup end");
  assert.throws(() => layout(clip, { startup: 7 }), /no length for phase active/);
});

test("monotone cubic: through every key, never past its neighbours, flat where keys are equal", () => {
  let seed = 7; const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
  for (let trial = 0; trial < 200; trial++) {
    const ts = [0], vs = [rnd()]; for (let i = 1; i < 6; i++) { ts.push(ts[i - 1] + 0.2 + rnd()); vs.push(rnd() < 0.2 ? vs[i - 1] : rnd() * 2 - 1); }
    for (let i = 0; i < ts.length; i++) near(monotoneCubic(ts, vs, ts[i]), vs[i], 1e-12, "key");
    for (let i = 0; i + 1 < ts.length; i++) for (let s = 0.05; s < 1; s += 0.1) {
      const v = monotoneCubic(ts, vs, ts[i] + s * (ts[i + 1] - ts[i])), lo = Math.min(vs[i], vs[i + 1]), hi = Math.max(vs[i], vs[i + 1]);
      assert.ok(v >= lo - 1e-12 && v <= hi + 1e-12, `overshoot between keys ${i} and ${i + 1}: ${v} not in [${lo}, ${hi}]`);
    }
  }
});

test("a looping clip wraps and closes on its first key", () => {
  const clip = { loop: true, frames: 20, keys: [{ at: 0, value: { y: 0 } }, { at: 0.5, value: { y: 1 } }] }, L = layout(clip);
  near(sampleClip(clip, L, 20).y, 0, 1e-12, "f = length"); near(sampleClip(clip, L, 30).y, 1, 1e-12, "f = 1.5 lengths");
});

test("phaseAt reports the phase and named windows", () => {
  const L = layout({ phases: ["startup", "active", "recovery"] }, { startup: 3, active: 2, recovery: 4 });
  assert.deepEqual(phaseAt(L, 3.5, { iframes: [1, 4] }), { phase: "active", done: false, iframes: true });
  assert.deepEqual(phaseAt(L, 9, { iframes: [1, 4] }), { phase: null, done: true, iframes: false });
});

test("warp: phase ends map to phase ends, monotone in between", () => {
  const ph = ["startup", "active", "recovery"], art = layout({ phases: ph }, { startup: 10, active: 4, recovery: 16 }), game = layout({ phases: ph }, { startup: 6, active: 3, recovery: 20 });
  near(warp(game, art, 6), 10, 1e-12, "startup end"); near(warp(game, art, 9), 14, 1e-12, "active end"); near(warp(game, art, 29), 30, 1e-12, "end");
  let prev = -1; for (let f = 0; f <= 29; f += 0.25) { const t = warp(game, art, f); assert.ok(t >= prev, "monotone"); prev = t; }
  near(warp(layout({ frames: 10 }), layout({ frames: 40 }), 5), 20, 1e-12, "no common phase: proportional");
});

test("two-bone IK reaches a target in reach, keeps both lengths, bends toward the pole", () => {
  const root = [0, 1.4, 0], mid = [0.05, 1.1, 0.02], end = [0.05, 0.8, 0.0], a = dist(root, mid), b = dist(mid, end);
  for (const target of [[0.3, 1.3, 0.35], [0, 1.0, 0.4], [-0.2, 0.9, 0.1]]) {
    const r = twoBone(root, mid, end, target, [0, 1.0, -1]);
    assert.ok(r.reached); near3(r.end, target, 1e-9, "end on target");
    near(dist(root, r.mid), a, 1e-9, "upper length"); near(dist(r.mid, r.end), b, 1e-9, "lower length");
    const d = [target[0] - root[0], target[1] - root[1], target[2] - root[2]], dl = Math.hypot(...d), u = d.map((x) => x / dl), pm = [r.mid[0] - root[0], r.mid[1] - root[1], r.mid[2] - root[2]], k = pm[0] * u[0] + pm[1] * u[1] + pm[2] * u[2];
    const off = [pm[0] - k * u[0], pm[1] - k * u[1], pm[2] - k * u[2]], pole = [0 - root[0], 1.0 - root[1], -1 - root[2]], kp = pole[0] * u[0] + pole[1] * u[1] + pole[2] * u[2];
    assert.ok(off[0] * (pole[0] - kp * u[0]) + off[1] * (pole[1] - kp * u[1]) + off[2] * (pole[2] - kp * u[2]) > 0, "the middle joint is on the pole's side");
  }
  const far = twoBone(root, mid, end, [0, 1.4, 5]);
  assert.ok(!far.reached); near(dist(root, far.end), a + b, 1e-6, "out of reach: fully extended toward the target");
});

test("rotationBetween carries one direction onto another", () => {
  for (const [u, v] of [[[1, 0, 0], [0, 1, 0]], [[0.3, -0.2, 0.9], [-0.5, 0.4, 0.1]], [[0, 0, 1], [0, 0, -1]]]) {
    const q = rotationBetween(u, v), r = qrot(q, u), lu = Math.hypot(...u), lv = Math.hypot(...v);
    near3(r.map((x) => x / lu), v.map((x) => x / lv), 1e-9, "rotated direction");
  }
});
