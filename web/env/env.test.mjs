// Environment state E0 checks (ADR 0017). Bounds: evidence/env-e0-prediction.json, written first.
import test from "node:test";
import assert from "node:assert/strict";
import { createEnvironment, loadEnvironment, ENV_VERSION } from "./state.mjs";
import { lightingFor, motionLook, wetMaterial, sway, stepSway, torch, stepTorch, sightRange } from "./readers.mjs";
import { sunDirection } from "./sky.mjs";

const DT = 1 / 60;
// A scripted stretch of play: a storm comes in, the door blows open, a stove heats the room.
function script(env, step) {
  if (step === 0) env.command({ type: "setZone", zone: "room", shelter: 1, lightScale: 0.5 }, { source: "level", reason: "the waiting room has a roof" });
  if (step === 60) env.command({ type: "setWeatherTarget", state: "rain", seconds: 10 }, { source: "quest", reason: "storm starts" });
  if (step === 900) env.command({ type: "setWeatherTarget", state: "storm", seconds: 5 }, { source: "quest", reason: "it gets worse" });
  if (step === 1000) env.command({ type: "addWind", zone: "room", vector: [6, 0], seconds: 2 }, { source: "door", reason: "the door blows open" });
  if (step === 1200) env.command({ type: "heat", zone: "room", degrees: 8, seconds: 30 }, { source: "stove", reason: "lit the stove" });
}
const make = (seed = 7, slots = 8) => createEnvironment({ seed, slots, zones: { room: {} }, start: { hour: 19, timeScale: 60, weather: "clear" } });
const run = (until, { seed = 7, from = null } = {}) => {
  const env = from || make(seed);
  for (let k = env.snapshot().step; k < until; k++) { script(env, k); env.step(DT); }
  return env;
};

test("same seed and commands give the same snapshot; another seed differs only where noise enters", () => {
  const a = run(2400).snapshot(), b = run(2400).snapshot(), c = run(2400, { seed: 8 }).snapshot();
  assert.deepStrictEqual(a, b);
  assert.notDeepStrictEqual(a.wind, c.wind);
  assert.equal(a.time.hour, c.time.hour);
  assert.ok(Object.isFrozen(a) && Object.isFrozen(a.zones.room) && Object.isFrozen(a.light.direct.colour));
  assert.throws(() => { "use strict"; a.light.balance = 0; }, TypeError);
});

test("save and load: a run saved halfway and resumed equals the straight run, through JSON", () => {
  const straight = run(2400);
  const half = run(1100), saved = JSON.parse(JSON.stringify(half.save()));
  assert.equal(saved.version, ENV_VERSION);
  const plain = run(2400, { from: loadEnvironment(saved) });
  assert.deepStrictEqual(plain.snapshot(), straight.snapshot());
  assert.deepStrictEqual(plain.log, straight.log);
  // A command queued before the save lands after the load.
  half.command({ type: "wet", zone: "outside", amount: 0 }, { source: "test", reason: "queued across a save" });
  const resumed = run(2400, { from: loadEnvironment(JSON.parse(JSON.stringify(half.save()))) });
  assert.equal(resumed.log.filter((e) => e.source === "test").length, 1);
  assert.throws(() => loadEnvironment({ ...saved, version: "raw-native.environment/0" }), /cannot load/);
});

test("slots: 8 by default, 6 accepted and equal to the 8-slot curve at its own hours, fewer refused", () => {
  assert.equal(createEnvironment().slots.length, 8);
  const six = createEnvironment({ slots: 6, start: { hour: 13 } }), eight = createEnvironment({ start: { hour: 13 } });
  assert.equal(six.slots.length, 6);
  assert.ok(Math.abs(six.snapshot().light.keyLevel - eight.snapshot().light.keyLevel) < 1e-12);
  assert.throws(() => createEnvironment({ slots: [0, 4, 8].map((h, i) => ({ name: `s${i}`, hour: h })) }), /too few/);
  assert.throws(() => createEnvironment({ library: { clear: { glow: Array(8).fill(1) } } }), /no unit and range/);
});

test("light composition: direct and ambient meet the key and the balance; explicit mode reports the balance", () => {
  for (const h of [0, 6, 12.5, 19.5]) {
    const L = createEnvironment({ start: { hour: h } }).snapshot().light;
    assert.ok(Math.abs(L.direct.intensity + L.ambient.intensity - L.keyLevel) < 1e-12);
    assert.ok(Math.abs(L.direct.intensity / L.keyLevel - L.balance) < 1e-12);
  }
  const lib = { clear: { direct: Array(8).fill(0.6), ambient: Array(8).fill(0.2) } };
  const e = createEnvironment({ composition: "explicit", library: lib }).snapshot();
  assert.ok(Math.abs(e.light.balance - 0.75) < 1e-12); assert.ok(Math.abs(e.light.keyLevel - 0.8) < 1e-12);
  // The sun is up at noon and down at midnight at 47.6 N in June.
  assert.ok(sunDirection(12.5, 172)[1] > 0.8); assert.ok(sunDirection(0, 172)[1] < 0);
});

test("rules: rain wets the open zone and not the sheltered one; it dries after; values stay in [0, 1]", () => {
  const env = run(1800), s = env.snapshot();
  assert.ok(s.zones.outside.wetness > 0.9, `outside ${s.zones.outside.wetness}`);
  assert.equal(s.zones.room.wetness, 0);
  assert.deepEqual(env.log.map((e) => e.cmd.type).slice(0, 3), ["setZone", "setWeatherTarget", "setWeatherTarget"]);
  const dry = createEnvironment({ start: { hour: 13, weather: "clear" } });
  dry.command({ type: "wet", zone: "outside", amount: 1 });
  let prev = 2;
  for (let k = 0; k < 3600; k++) { const z = dry.step(DT).zones.outside; assert.ok(z.wetness <= prev && z.wetness >= 0); prev = z.wetness; }
  assert.ok(prev < 0.9, `dried to ${prev} in a minute`);
});

test("events: rain-start once, wet, thunder in the storm; the door's gust reaches the room only while it lasts", () => {
  const env = make(), seen = [], roomWind = [];
  env.subscribe((s) => { for (const e of s.events) seen.push(e.type); roomWind.push(s.zones.room.windSpeed); });
  for (let k = 0; k < 2400; k++) { script(env, k); env.step(DT); }
  for (const t of ["rain-start", "wet", "thunder", "command"]) assert.ok(seen.includes(t), t);
  assert.equal(seen.filter((t) => t === "rain-start").length, 1, "hysteresis: one start, no chatter");
  assert.ok(roomWind[1001] > 4 && roomWind[1001] <= 6, `room wind ${roomWind[1001]}`); assert.equal(roomWind[1200], 0);
});

test("reader (a): lighting and the look follow the weather", () => {
  const storm = run(2400).snapshot(), clear = createEnvironment({ start: { hour: storm.time.hour } }).snapshot();
  const a = motionLook(clear), b = motionLook(storm);
  assert.ok(b.brightness < a.brightness, `storm ${b.brightness} vs clear ${a.brightness}`);
  assert.ok(b.fog.amount > a.fog.amount); assert.ok(b.post.bloom > a.post.bloom);
  assert.ok(lightingFor(storm, "outside").balance < lightingFor(clear, "outside").balance);
  const inside = lightingFor(storm, "room"), outside = lightingFor(storm, "outside");
  assert.ok(inside.key < outside.key && inside.shadowStrength === 0);
});

test("reader (b): wet surfaces darken by porosity and get glossier, as specified", () => {
  const m = { albedo: [0.5, 0.4, 0.3], roughness: 0.7, porosity: 1 };
  assert.deepEqual(wetMaterial(m, 0).albedo, m.albedo); assert.equal(wetMaterial(m, 0).roughness, 0.7);
  const w = wetMaterial(m, 1);
  assert.ok(Math.abs(w.albedo[0] - 0.5 * 0.55) < 1e-12); assert.ok(Math.abs(w.roughness - (0.7 - 0.64 * 0.85)) < 1e-12);
  assert.ok(Math.abs(w.peakGain - (0.7 / w.roughness) ** 4) < 1e-9 && w.peakGain > 100);
  let prev = wetMaterial(m, 0);
  for (let x = 0.05; x <= 1; x += 0.05) { const c = wetMaterial(m, x); assert.ok(c.albedo[0] <= prev.albedo[0] + 1e-15 && c.roughness <= prev.roughness + 1e-15); prev = c; }
  assert.ok(wetMaterial({ ...m, porosity: 0.2 }, 1).albedo[0] > w.albedo[0]);
  assert.equal(wetMaterial({ albedo: [1, 1, 1], roughness: 0.05 }, 1).roughness, 0.05);
});

test("reader (c): sway rests in still air and settles to response * wind / stiffness in a steady wind", () => {
  let s = sway();
  for (let k = 0; k < 600; k++) s = stepSway(s, [0, 0], DT);
  assert.equal(s.angle, 0);
  for (const v of [2, 6, -6]) {
    s = sway(); for (let k = 0; k < 1200; k++) s = stepSway(s, [v, 0], DT);
    const eq = (s.response * v) / s.stiffness;
    assert.ok(Math.abs(s.angle - eq) < 0.01 * Math.abs(eq), `wind ${v}: ${s.angle} vs ${eq}`);
  }
  s = sway({ axis: [0, 1] }); for (let k = 0; k < 600; k++) s = stepSway(s, [10, 0], DT);
  assert.equal(s.angle, 0, "wind along the element does not sway it");
});

test("gameplay: rain puts the torch out outside, not under the roof; relight needs shelter; fog shortens sight", () => {
  const s = run(1800).snapshot();
  let out = torch(), inn = torch(), when = null;
  for (let k = 0; k < 3600; k++) {
    const a = stepTorch(out, s, "outside", DT); out = a.state; if (a.events.length && when === null) when = (k + 1) * DT;
    inn = stepTorch(inn, s, "room", DT).state;
  }
  const expect = 1 / (s.zones.outside.rain * 0.006);
  assert.ok(!out.lit && Math.abs(when - expect) < 0.05, `out after ${when} s, expected ${expect}`);
  assert.ok(inn.lit);
  assert.equal(stepTorch(out, s, "outside", DT, { relight: true }).state.lit, false);
  let t = { lit: false, soak: 1 };
  for (let k = 0; k < 1200; k++) t = stepTorch(t, s, "room", DT, { relight: true }).state;
  assert.ok(t.lit, "relit under the roof once dry enough");
  const fog = createEnvironment({ start: { hour: 12.5, weather: "fog" } }).snapshot(), clear = createEnvironment({ start: { hour: 12.5 } }).snapshot();
  assert.ok(sightRange(fog, "outside") < 0.25 * sightRange(clear, "outside"));
  assert.equal(sightRange(createEnvironment({ start: { hour: 0, weather: "fog" } }).snapshot(), "outside", { carried: 6 }), 6);
});
