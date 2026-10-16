// Low Water, the vertical slice: rules checked without rendering. Bounds in
// evidence/slice-low-water-prediction.json, committed before these tests ran.
import test from "node:test";
import assert from "node:assert/strict";
import slice, { game } from "./slice.mjs";
import { solidAt, LAMP_POSTS } from "./slice/level.mjs";
import { stepPrints, stepLamps, ERASE_PER_MMH, FADE } from "./slice/rules.mjs";
import { createSession } from "../../web/game/loop.mjs";
import { createActionMap } from "../../web/game/input.mjs";

const play = async (steps) => {
  const ctx = { motion: null }; ctx.assets = await slice.load(ctx);
  const map = createActionMap(game.actions), s = createSession(game, { assets: ctx.assets }), seen = [s.state];
  for (let i = 0; i < steps; i++) { s.stepN(1, (k) => game.demo(k, map, s.state)); seen.push(s.state); }
  return { ctx, seen };
};

test("the film is the same frame however it is seeked", async () => {
  const a = { motion: null }; a.assets = await slice.load(a);
  const b = { motion: null, assets: a.assets }, ts = [0, 7.5, 21, 33.5, 39.9];
  const key = (l) => JSON.stringify({ camera: l.camera, n: l.items.length });
  const fwd = ts.map((t) => key(slice.frame(t, a))), back = [...ts].reverse().map((t) => key(slice.frame(t, b))).reverse();
  assert.deepEqual(fwd, back);
  assert.equal(key(slice.frame(21, { motion: null, assets: a.assets })), fwd[2]);
});

test("the scripted walk: never inside a wall or a prop; the lamp drowns on the quay and is relit at the stove", async () => {
  const { seen } = await play(2400);
  for (const st of seen) for (const [dx, dy] of [[8.9, 0], [-8.9, 0], [0, 8.9], [0, -8.9]]) assert.ok(!solidAt(st.p[0] + dx, st.p[1] + dy, st.doorOpen), `inside something at ${st.p}`);
  const out = seen.findIndex((st) => !st.lamp.lit), back = seen.findIndex((st, i) => i > out && st.lamp.lit);
  assert.ok(out > 0 && seen[out].snap.weather.weights.storm > 0.5 && seen[out].p[0] > 17 * 24, "out in the storm on the quay");
  assert.ok(back > out && Math.hypot(seen[back].p[0] - 132, seen[back].p[1] - 180) < 64 && !seen[back].doorOpen, "relit at the stove, door shut");
  assert.ok(seen.some((st) => st.prints.length > 0), "footprints were left");
  assert.ok(seen.at(-1).snap.weather.weights.fog > 0.9);
});

test("door rule: open in rain, the room's wetness rises; shut, it holds", async () => {
  const { seen } = await play(1710);
  const openStretch = seen.slice(1400, 1700).map((st) => st.snap.zones.shelter.wetness);
  assert.ok(seen[1400].doorOpen && openStretch.every((w, i) => i === 0 || w > openStretch[i - 1]), "rising while open");
  assert.equal(seen[1400].env.log.filter((e) => e.cmd.type === "setZone").at(-1).cmd.shelter, 0.95);
  const { seen: later } = await play(2000), shut = later.slice(1720, 2000).map((st) => st.snap.zones.shelter.wetness);
  assert.ok(!later[1720].doorOpen && shut.every((w, i) => i === 0 || w <= shut[i - 1]), "never rising while shut");
});

test("footprints: one every 0.45 s on wet ground; rain erases them at the stated rate; dry weather keeps them", () => {
  const dt = 1 / 60, walk = { pos: [500, 300], facing: 0, moving: true, wetness: 0.8, open: 1 };
  let prints = [], since = 0, made = 0;
  for (let i = 0; i < 270; i++) { const r = stepPrints(prints, walk, 0, dt, since); if (r.prints.length > prints.length) made++; prints = r.prints; since = r.since; }
  assert.equal(made, 10, "4.5 s of walking at one print per 0.45 s");
  for (const rain of [8, 30]) {
    let p = [{ x: 0, y: 0, a: 0, s: 1, open: 1 }], k = 0;
    while (p.length) { p = stepPrints(p, { ...walk, moving: false }, rain, dt, 0).prints; k++; }
    const expect = 0.98 / (ERASE_PER_MMH * rain + FADE);
    assert.ok(Math.abs(k * dt - expect) < 0.1, `rain ${rain}: gone after ${k * dt} s, expected ${expect}`);
  }
  let p = [{ x: 0, y: 0, a: 0, s: 1, open: 1 }];
  for (let i = 0; i < 60 * 60 * 8; i++) p = stepPrints(p, { ...walk, moving: false }, 0, dt, 0).prints;
  assert.equal(p.length, 1, "still there after 8 minutes of dry weather");
});

test("street lamps come on in order, 0.4 s apart, when the quay turns dark", () => {
  let lamps = LAMP_POSTS.map(() => ({ on: false, level: 0, wait: 0 })); const on = [];
  for (let i = 0; i < 300; i++) { lamps = stepLamps(lamps, true, 1 / 60); lamps.forEach((l, k) => { if (l.on && on[k] === undefined) on[k] = i; }); }
  assert.deepEqual(on.map((v, k) => v - on[0]), [0, 24, 48]);
  assert.ok(lamps.every((l) => l.level === 1));
  for (let i = 0; i < 300; i++) lamps = stepLamps(lamps, false, 1 / 60);
  assert.ok(lamps.every((l) => !l.on && l.level === 0));
});
