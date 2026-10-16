// G1 runtime checks; bounds are in evidence/g1-runtime-prediction.json (written first).
import test from "node:test";
import assert from "node:assert/strict";
import { createFixedLoop, createSession } from "./loop.mjs";
import { createActionMap, emptyDevice, createRecorder, replay, captureBinding, frame, parseBinding } from "./input.mjs";
import { view2d, stepView2d, toDesign, toScene, firstPerson, stepFirstPerson, fpRay, fpBasis } from "./cameras.mjs";
import { rayAabb, raySphere, rayTriangle, pickScene, pickItems, invertAffine } from "./pick.mjs";
import { filmFrame } from "./film.mjs";
import { Scene, compose, mul } from "../world/scene.mjs";
import { isoCamera } from "../world/iso.mjs";

// A small deterministic generator for the tests' random inputs.
const lcg = (s) => () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);

// A toy game with nonlinear state, so any change in the number or order of steps shows.
const toy = {
  hz: 60,
  actions: { moveX: { type: "axis", bind: ["key:KeyD", "-key:KeyA", "pad.axis:0"] }, moveY: { type: "axis", bind: ["key:KeyS", "-key:KeyW", "pad.axis:1"] }, use: { type: "button", bind: ["key:Space", "pointer:0", "pad.button:0"] } },
  init: (seed) => ({ x: 0, y: 0, vx: 0, vy: 0, uses: 0, phase: seed * 0.1, trail: 0 }),
  step(s, f, dt) {
    const ax = f.axes.moveX || 0, ay = f.axes.moveY || 0;
    const vx = s.vx * Math.exp(-3 * dt) + ax * 8 * dt, vy = s.vy * Math.exp(-3 * dt) + ay * 8 * dt;
    const phase = s.phase + dt * (1 + Math.sin(s.x));
    return { x: s.x + vx * dt, y: s.y + vy * dt, vx, vy, uses: s.uses + (f.pressed.use ? 1 : 0), phase, trail: s.trail * 0.99 + Math.sin(phase) * (f.pointer ? f.pointer[0] / 1920 : 0.5) };
  },
  lerp: (a, b, t) => ({ ...b, x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t }),
};
const steadyInput = (i) => ({ held: { use: i % 50 === 0 }, pressed: { use: i % 50 === 0 }, released: {}, axes: { moveX: Math.sin(i * 0.05), moveY: Math.cos(i * 0.031) }, pointer: [i % 1920, 540] });

test("fixed step: the same total time in any partition gives the same steps and the same state", () => {
  const totalUs = 10_000_000, runs = [];
  const partitions = {
    even: Array(600).fill(1 / 60),
    irregular: [0.5, 3.25, 0.001, 2.2, 1.9, 1.0, 1.149],
    random: (() => { const r = lcg(7), raw = Array.from({ length: 1000 }, () => 0.0001 + r() * 0.0329), us = raw.map((v) => Math.round(v * 1e6)); const sum = us.reduce((a, b) => a + b, 0); const k = totalUs / sum; const sc = us.map((v) => Math.floor(v * k)); sc[0] += totalUs - sc.reduce((a, b) => a + b, 0); return sc.map((v) => v / 1e6); })(),
  };
  for (const [name, frames] of Object.entries(partitions)) {
    // 1/60 s is not a whole number of microseconds: 600 frames of it count 10.0002 s (each is
    // rounded to 16667 us, 20 parts per million slow), still 600 steps. The others sum exactly.
    assert.equal(frames.reduce((a, b) => a + Math.round(b * 1e6), 0), name === "even" ? 10_000_200 : totalUs, name);
    const s = createSession(toy, { seed: 3, maxSteps: 100000 });
    for (const f of frames) s.advance(f, steadyInput);
    runs.push([name, s.loop.steps, s.state]);
  }
  for (const [name, steps, state] of runs) { assert.equal(steps, 600, name); assert.deepStrictEqual(state, runs[0][2], name); }
});

test("fixed step: alpha is the remainder, and the stall bound drops time and reports it", () => {
  const loop = createFixedLoop({ hz: 60, maxSteps: 8 });
  let r = loop.advance(0.025, () => {});
  assert.equal(r.steps, 1); assert.ok(Math.abs(r.alpha - (0.025 * 60 - 1)) < 1e-6);
  r = loop.advance(2, () => {});
  assert.equal(r.steps, 8);
  assert.ok(Math.abs(r.dropped - (Math.floor((0.025 + 2) * 60) - 9) / 60) < 1e-9, `dropped ${r.dropped}`);
  const l2 = createFixedLoop({ hz: 60, maxSteps: 8 }); r = l2.advance(2, () => {});
  assert.ok(Math.abs(r.dropped - 112 / 60) < 1e-9);
  assert.throws(() => loop.advance(-1, () => {}), /finite/); assert.throws(() => createFixedLoop({ hz: 0 }), /hz/);
  // The interpolated view sits between the previous and the current state.
  const s = createSession(toy, { seed: 1 }); s.advance(0.5 + 0.4 / 60, steadyInput);
  const v = s.view(), lo = Math.min(s.previous.x, s.state.x), hi = Math.max(s.previous.x, s.state.x);
  assert.ok(v.x >= lo && v.x <= hi); assert.ok(Math.abs(s.alpha - 0.4) < 1e-4);
});

test("input: a recorded live session replays to the identical state, through JSON", () => {
  const map = createActionMap(toy.actions), dev = emptyDevice(), r = lcg(99), rec = createRecorder();
  const live = createSession(toy, { seed: 5 });
  const liveInput = rec.wrap(() => {
    // A noisy person: keys go down and up, the stick drifts, the pointer taps.
    for (const k of ["KeyW", "KeyA", "KeyS", "KeyD", "Space"]) { const u = r(); if (u < 0.05) { dev.keys.add(k); dev.taps.add(k); } else if (u < 0.1) dev.keys.delete(k); }
    dev.padAxes = [r() * 2 - 1, r() * 2 - 1]; dev.padButtons = [r() < 0.02 ? 1 : 0];
    dev.pointer = { x: r() * 1920, y: r() * 1080, buttons: new Set(), taps: new Set(r() < 0.03 ? [0] : []) };
    const f = map.sample(dev); dev.taps.clear(); return f;
  });
  const fr = lcg(3);
  for (let k = 0; k < 400; k++) live.advance(0.004 + fr() * 0.03, liveInput);
  const track = JSON.parse(JSON.stringify(rec.toJSON()));
  assert.equal(track.version, "raw-native.input-track/1");
  const again = createSession(toy, { seed: 5 });
  again.stepN(live.loop.steps, replay(track));
  assert.ok(live.loop.steps > 100);
  assert.deepStrictEqual(again.state, live.state);
  assert.ok(live.state.uses > 0, "some presses happened");
});

test("input: taps, dead zone, clamping, rebinding and capture", () => {
  const map = createActionMap(toy.actions), dev = emptyDevice();
  dev.taps.add("Space");                                     // down and up between samples
  let f = map.sample(dev); dev.taps.clear();
  assert.equal(f.pressed.use, true); assert.equal(f.held.use, true);
  f = map.sample(dev); assert.equal(f.released.use, true); assert.equal(f.held.use, false);
  f = map.sample(dev); assert.equal(f.released.use, false);
  dev.padAxes = [0.15, -1]; dev.keys.add("KeyD");
  f = map.sample(dev); assert.equal(f.axes.moveX, 1); assert.equal(f.axes.moveY, -1);   // dead zone, then clamp
  dev.keys.clear(); dev.padAxes = [0.6, 0];
  assert.ok(Math.abs(map.sample(dev).axes.moveX - 0.5) < 1e-12);                       // rescaled past the dead zone
  const old = map.rebind("use", ["key:KeyE"]);
  assert.deepEqual(old, ["key:Space", "pointer:0", "pad.button:0"]);
  dev.keys.add("Space"); assert.equal(map.sample(dev).held.use, false);
  dev.keys.add("KeyE"); assert.equal(map.sample(dev).held.use, true);
  const back = createActionMap(JSON.parse(JSON.stringify(map.toJSON())));
  assert.deepEqual(back.bindings("use"), ["key:KeyE"]);
  const base = emptyDevice(), now = emptyDevice(); now.keys.add("KeyQ");
  assert.equal(captureBinding(now, base), "key:KeyQ");
  now.keys.clear(); now.padAxes = [0, -0.9]; assert.equal(captureBinding(now, base), "-pad.axis:1");
  assert.throws(() => parseBinding("mouse:1"), /bad binding/); assert.throws(() => parseBinding("pad.axis:x"), /number/);
  assert.deepEqual(frame(map, { axes: { moveX: 3 } }).axes.moveX, 1);
});

test("picking: rays against boxes, spheres and triangles in a G0 scene", () => {
  const s = new Scene(), q = Math.SQRT1_2;
  const a = s.add({ id: "box", kind: "mesh", t: [0, 0, -5], props: { bounds: { min: [-0.5, -0.5, -0.5], max: [0.5, 0.5, 0.5] } } });
  const grp = s.add({ id: "grp", t: [4, 0, 0] });
  s.add({ id: "long", kind: "mesh", parent: grp, t: [0, 0, -5], r: [0, q, 0, q], s: [2, 1, 1], props: { bounds: { min: [-0.5, -0.5, -0.5], max: [0.5, 0.5, 0.5] } } });
  s.add({ id: "tri", kind: "mesh", t: [-4, 0, -3], props: { triangles: [[-1, -1, 0, 1, -1, 0, 0, 1, 0]] } });
  const near = (x, y) => Math.abs(x - y) < 1e-9;
  let h = pickScene(s, { origin: [0, 0, 0], dir: [0, 0, -1] }); assert.equal(h.id, "box"); assert.ok(near(h.t, 4.5));
  h = pickScene(s, { origin: [4, 0, 0], dir: [0, 0, -1] }); assert.equal(h.id, "long"); assert.ok(near(h.t, 4), `t ${h.t}`);
  h = pickScene(s, { origin: [-4, 0, 0], dir: [0, 0, -1] }); assert.equal(h.id, "tri"); assert.ok(near(h.t, 3));
  assert.deepEqual(h.point.map((v) => +v.toFixed(9)), [-4, 0, -3]);
  assert.equal(pickScene(s, { origin: [0, 3, 0], dir: [0, 0, -1] }), null);
  // Nearest wins when two boxes line up.
  s.add({ id: "front", kind: "mesh", t: [0, 0, -2], props: { bounds: { min: [-0.5, -0.5, -0.5], max: [0.5, 0.5, 0.5] } } });
  h = pickScene(s, { origin: [0, 0, 0], dir: [0, 0, -1] }); assert.equal(h.id, "front"); assert.ok(near(h.t, 1.5));
  assert.equal(pickScene(s, { origin: [0, 0, 0], dir: [0, 0, -1] }, { filter: (n) => n.id !== "front" }).id, "box");
  // Primitives directly; the affine inverse undoes a composed matrix.
  assert.ok(near(raySphere([0, 0, 0], [1, 0, 0], [5, 0, 0], 1), 4)); assert.equal(raySphere([0, 0, 0], [-1, 0, 0], [5, 0, 0], 1), null);
  assert.equal(rayTriangle([0, 0, 1], [0, 0, -1], [2, 2, 0], [3, 2, 0], [2, 3, 0]), null);
  assert.equal(rayAabb([0, 0, 0], [0, 1, 0], [1, -1, -1], [2, 1, 1]), null);
  const m = compose([1, 2, 3], [0, Math.sin(0.3), 0, Math.cos(0.3)], [2, 3, 0.5]), id = mul(invertAffine(m), m);
  for (let k = 0; k < 16; k++) assert.ok(Math.abs(id[k] - (k % 5 === 0 ? 1 : 0)) < 1e-12);
  assert.equal(pickItems([{ id: "under", rect: [0, 0, 100, 100] }, { id: "over", circle: [50, 50, 10] }], [52, 50]), "over");
  assert.equal(pickItems([{ id: "under", rect: [0, 0, 100, 100] }, { id: "over", circle: [50, 50, 10] }], [5, 5]), "under");
});

test("picking: every isometric cell is found under its centre through the Motion camera", () => {
  const iso = isoCamera({ tileW: 64, tileH: 32, origin: [960, 140], width: 24, height: 24 });
  const cam = { ...view2d({ x: 1000, y: 520, zoom: 1.3 }) };
  for (let j = 0; j < 24; j++) for (let i = 0; i < 24; i++) {
    const d = toDesign(cam, iso.toScreen(i + 0.5, j + 0.5));         // where the cell centre lands on screen
    assert.deepEqual(iso.pick(...toScene(cam, d)), [i, j]);
  }
  assert.equal(iso.pick(...toScene(cam, toDesign(cam, iso.toScreen(-0.5, 3)))), null);
});

test("cameras: zoom keeps the anchored point fixed; first person walks, slides on walls and casts rays", () => {
  let c = view2d({ x: 300, y: 200, zoom: 1, minZoom: 0.25, maxZoom: 8 }); const anchor = [1500, 300];
  const before = toScene(c, anchor);
  for (let k = 0; k < 120; k++) c = stepView2d(c, 1 / 60, { zoomSteps: k < 10 ? 1 : 0, anchor });
  assert.ok(c.zoom < 0.5); const after = toScene(c, anchor);
  assert.ok(Math.hypot(after[0] - before[0], after[1] - before[1]) < 1e-6, `drift ${after} vs ${before}`);
  // Follow converges; pan moves in scene units divided by zoom.
  let f = view2d(); for (let k = 0; k < 600; k++) f = stepView2d(f, 1 / 60, { follow: [100, 50] });
  assert.ok(Math.hypot(f.x - 100, f.y - 50) < 1e-6);
  // First person: forward is -z at yaw 0; a wall at x > 1 stops a strafe but not the walk.
  let p = firstPerson({ pos: [0, 1.6, 0] });
  assert.deepEqual(fpBasis(p).fwd.map((v) => +v.toFixed(12)), [-0, 0, -1].map((v) => +v.toFixed(12)));
  for (let k = 0; k < 60; k++) p = stepFirstPerson(p, 1 / 60, { moveY: 1, moveX: 1, speed: 3, solid: (x) => x > 1 });
  assert.ok(p.pos[0] <= 1 - p.radius + 1e-9 && p.pos[0] > 0.7, `x ${p.pos[0]}`); assert.ok(p.pos[2] < -2, `z ${p.pos[2]}`);
  const r = fpRay(firstPerson(), 0, 0, 16 / 9); assert.ok(Math.abs(r.dir[2] + 1) < 1e-12);
});

test("film: a game replays its demo to the same frame at any t, seeking back or forward", () => {
  const game = { ...toy, demo: (i) => steadyInput(i), view: (v) => ({ items: [], x: v.x, uses: v.uses }) };
  const ctxA = { assets: null }, ctxB = { assets: null };
  const seq = [0, 0.5, 1, 3.3, 2, 7.25, 7.25];
  const a = seq.map((t) => filmFrame(game, ctxA, t));
  const b = seq.map((t) => filmFrame(game, ctxB, t));
  assert.deepEqual(a, b);
  const fresh = filmFrame(game, { assets: null }, 2);
  assert.deepEqual(fresh, a[4], "seeking back resimulates to the same frame");
  assert.ok(a[5].uses >= 8);
});
