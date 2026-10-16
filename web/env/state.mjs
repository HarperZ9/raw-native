// The environment state hub (ADR 0017, first slice E0). Pure: runs in Node.
//
// One typed, versioned, serialisable record that lighting, materials, animation and gameplay
// read as a frozen snapshot, and change only through logged commands applied at the step
// boundary. It steps on the G1 fixed step (web/game/loop.mjs), so the same seed and the same
// commands give the same snapshot on every run and every machine.
//
//   const env = createEnvironment({ seed: 7, slots: 8, start: { hour: 19, weather: "clear" } });
//   env.command({ type: "setWeatherTarget", state: "rain", seconds: 20 }, { source: "quest", reason: "storm starts" });
//   env.step(1 / 60);                 // apply queued commands, advance time, weather, wind and rules
//   const s = env.snapshot();         // frozen; s.light.balance, s.zones.outside.wetness, ...
//   env.subscribe((snap) => ...);     // called after every step; snap.events lists what happened
//   const saved = env.save();  const back = loadEnvironment(saved);
//
// Commands (priority order when several land on one step, then issue order):
//   setTime { hour, day? }  setTimeScale { scale }  setZone { zone, shelter?, lightScale? }
//   setWeatherTarget { state, seconds }  addWind { zone, vector: [x, z], seconds }
//   heat { zone, degrees, seconds }  wet { zone, amount }
// Rules (per second of simulation, every zone): wetness rises with rain that reaches the zone
// (rain * (1 - shelter)) and falls with wind, dry air and warmth, bounded in [0, 1]; frost
// forms below 0 C when moisture is present and thaws above; dust lifts when dry and windy.
import { createLookKeys, resolveSlots, SLOTS_8 } from "../look/keys.mjs";
import { TERMS, DEFAULT_LIBRARY } from "./terms.mjs";
import { sunDirection, moonDirection, hash01, noise1 } from "./sky.mjs";

export const ENV_VERSION = "raw-native.environment/1";
const PRIORITY = ["setTime", "setTimeScale", "setZone", "setWeatherTarget", "addWind", "heat", "wet"];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const smooth = (t) => t * t * (3 - 2 * t);
// Hysteresis bands for the events gameplay listens to: [on above, off below] (dark is inverted).
export const BANDS = { rain: [1, 0.3], wet: [0.35, 0.25], dark: [0.12, 0.18] };

function buildCurves(library, slots) {
  const list = resolveSlots(slots), curves = {};
  if (!library.clear) throw new Error("environment: the weather library needs a clear state");
  for (const [w, terms] of Object.entries(library)) {
    const params = {};
    for (const [name, values] of Object.entries({ ...library.clear, ...terms })) {
      const T = TERMS[name];
      if (!T) throw new Error(`environment: term ${name} has no unit and range, so it cannot enter the state`);
      let v = values;
      if (v.length !== list.length) {
        if (v.length !== SLOTS_8.length) throw new Error(`environment: ${w}.${name} needs ${list.length} values or the 8 default slots`);
        const k = createLookKeys({ slots: 8, params: { x: v } });
        v = list.map((s) => k.term("x", s.hour));
      }
      params[name] = { values: v, min: T.min, max: T.max };
    }
    curves[w] = createLookKeys({ slots: list, params });
  }
  return { list, curves };
}

const newZone = (z = {}) => ({ shelter: 0, lightScale: 1, wetness: 0, frost: 0, dust: 0, heat: 0, heatFrom: 0, heatTo: 0, heatLeft: 0, heatTotal: 0, boosts: [], dark: false, wet: false, ...z });

export function createEnvironment({ seed = 1, slots = 8, library = DEFAULT_LIBRARY, latitude = 47.6, composition = "key-balance", zones = {}, start = {} } = {}, restored = null) {
  const { list, curves } = buildCurves(library, slots);
  if (composition !== "key-balance" && composition !== "explicit") throw new Error(`environment: composition is key-balance or explicit, got ${composition}`);
  const weathers = Object.keys(library), w0 = start.weather || "clear";
  if (!weathers.includes(w0)) throw new Error(`environment: no weather state ${w0}`);
  const one = (name) => Object.fromEntries(weathers.map((w) => [w, w === name ? 1 : 0]));
  const st = restored ? structuredClone(restored.state) : {
    step: 0, clock: 0, hour: start.hour ?? 12, day: start.day ?? 172, timeScale: start.timeScale ?? 0,
    weights: one(w0), from: one(w0), target: w0, blendLeft: 0, blendTotal: 0, rainOn: false, flash: 0, seq: 0, queue: [],
    zones: Object.fromEntries(Object.entries({ outside: {}, ...zones }).map(([n, z]) => [n, newZone(z)])),
  };
  const log = restored ? structuredClone(restored.log) : [];
  const subs = new Set();
  let snap = null, events = [];
  const zone = (n) => { if (!st.zones[n]) throw new Error(`environment: no zone ${n}`); return st.zones[n]; };

  // The blended value of every term at the current hour: the weight-sum of the states' curves.
  function terms() {
    const out = {};
    for (const [w, k] of Object.entries(st.weights)) {
      if (k <= 0) continue;
      const v = curves[w].at(st.hour);
      for (const [n, x] of Object.entries(v)) out[n] = Array.isArray(x) ? x.map((c, i) => (out[n] ? out[n][i] : 0) + c * k) : (out[n] || 0) + x * k;
    }
    return out;
  }

  function apply(c) {
    switch (c.type) {
      case "setTime": st.hour = ((c.hour % 24) + 24) % 24; if (c.day !== undefined) st.day = c.day; break;
      case "setTimeScale": st.timeScale = c.scale; break;
      case "setZone": { const z = st.zones[c.zone] || (st.zones[c.zone] = newZone()); if (c.shelter !== undefined) z.shelter = clamp(c.shelter, 0, 1); if (c.lightScale !== undefined) z.lightScale = Math.max(0, c.lightScale); break; }
      case "setWeatherTarget":
        if (!weathers.includes(c.state)) throw new Error(`environment: no weather state ${c.state}`);
        st.from = { ...st.weights }; st.target = c.state; st.blendTotal = st.blendLeft = Math.max(0, c.seconds ?? 0);
        if (st.blendTotal === 0) st.weights = one(c.state);
        break;
      case "addWind": zone(c.zone).boosts.push({ v: [...c.vector], left: c.seconds, total: c.seconds }); break;
      case "heat": { const z = zone(c.zone); z.heatFrom = z.heat; z.heatTo = z.heat + c.degrees; z.heatTotal = z.heatLeft = Math.max(1e-9, c.seconds ?? 0); break; }
      case "wet": { const z = zone(c.zone); z.wetness = clamp(z.wetness + c.amount, 0, 1); break; }
      default: throw new Error(`environment: unknown command ${c.type}`);
    }
  }

  function step(dt) {
    events = [];
    // Commands land at the step boundary, by priority and then by issue order.
    const q = st.queue.sort((a, b) => PRIORITY.indexOf(a.cmd.type) - PRIORITY.indexOf(b.cmd.type) || a.seq - b.seq);
    st.queue = [];
    for (const e of q) { apply(e.cmd); log.push({ step: st.step, ...e }); events.push({ type: "command", command: e.cmd.type, source: e.source, reason: e.reason }); }
    st.clock += dt; st.step++;
    st.hour = (((st.hour + (dt * st.timeScale) / 3600) % 24) + 24) % 24;
    if (st.blendLeft > 0) {
      st.blendLeft = Math.max(0, st.blendLeft - dt);
      const p = smooth(1 - st.blendLeft / st.blendTotal), to = one(st.target);
      for (const w of weathers) st.weights[w] = st.from[w] + (to[w] - st.from[w]) * p;
    }
    const T = terms(), wind = windOf(T);
    // Thunder in a storm: a strike is a seeded draw per step, rate one per 9 s at full storm.
    const storm = st.weights.storm || 0;
    st.flash *= Math.exp(-dt * 7);
    if (storm > 0 && hash01(seed, 3, st.step) < (storm * dt) / 9) {
      const km = 0.5 + 3.5 * hash01(seed, 4, st.step);
      st.flash = Math.max(st.flash, 1 / (1 + km * 0.6));
      events.push({ type: "thunder", distanceKm: km, delaySeconds: (km * 1000) / 343 });
    }
    if (!st.rainOn && T.rainRate > BANDS.rain[0]) { st.rainOn = true; events.push({ type: "rain-start" }); }
    if (st.rainOn && T.rainRate < BANDS.rain[1]) { st.rainOn = false; events.push({ type: "rain-stop" }); }
    for (const [n, z] of Object.entries(st.zones)) stepZone(n, z, T, wind, dt);
    snap = null;
    const s = snapshot();
    for (const f of subs) f(s);
    return s;
  }

  function windOf(T) {
    const dir = 0.6 + 0.9 * noise1(seed, 1, st.clock / 40), g = noise1(seed, 2, st.clock / 2.5);
    const speed = Math.max(0, T.windSpeed * (1 + T.gustiness * g));
    return { dir: [Math.cos(dir), Math.sin(dir)], speed, gust: g };
  }

  function stepZone(n, z, T, wind, dt) {
    for (const b of z.boosts) b.left -= dt;
    z.boosts = z.boosts.filter((b) => b.left > 0);
    if (z.heatLeft > 0) { z.heatLeft = Math.max(0, z.heatLeft - dt); z.heat = z.heatFrom + (z.heatTo - z.heatFrom) * (1 - z.heatLeft / z.heatTotal); }
    const open = 1 - z.shelter, rainIn = T.rainRate * open, wv = zoneWind(z, wind), windEff = Math.hypot(wv[0], wv[1]);
    const temp = T.temperature + z.heat;
    const wetting = (0.08 * rainIn) / (rainIn + 4);
    const drying = 0.004 * (1 + windEff / 4) * (1 - T.humidity) * clamp((temp + 10) / 30, 0, 2);
    z.wetness = clamp(z.wetness + (wetting - drying) * dt, 0, 1);
    if (temp < 0 && (z.wetness > 0.05 || T.humidity > 0.85)) z.frost = clamp(z.frost + 0.01 * dt * Math.min(1, -temp / 5), 0, 1);
    else if (temp > 0) z.frost = clamp(z.frost - 0.02 * dt * Math.min(1, temp / 5), 0, 1);
    const dustGoal = clamp((windEff - 6) / 8, 0, 1) * (1 - z.wetness) ** 2 * (1 - T.humidity);
    z.dust += (dustGoal - z.dust) * (1 - Math.exp(-0.2 * dt));
    const light = T.keyLevel * z.lightScale + st.flash * 0.8 * open;
    if (!z.dark && light < BANDS.dark[0]) { z.dark = true; events.push({ type: "dark", zone: n }); }
    if (z.dark && light > BANDS.dark[1]) { z.dark = false; events.push({ type: "light", zone: n }); }
    if (!z.wet && z.wetness > BANDS.wet[0]) { z.wet = true; events.push({ type: "wet", zone: n }); }
    if (z.wet && z.wetness < BANDS.wet[1]) { z.wet = false; events.push({ type: "dry", zone: n }); }
  }
  const zoneWind = (z, wind) => {
    const k = 1 - z.shelter, v = [wind.dir[0] * wind.speed * k, wind.dir[1] * wind.speed * k];
    for (const b of z.boosts) { const f = b.left / b.total; v[0] += b.v[0] * f; v[1] += b.v[1] * f; }
    return v;
  };

  function snapshot() {
    if (snap) return snap;
    const T = terms(), wind = windOf(T), sun = sunDirection(st.hour, st.day, latitude), moon = moonDirection(st.hour, st.day, latitude);
    let direct, ambient;
    if (composition === "explicit") { direct = T.direct ?? 0; ambient = T.ambient ?? 0; }
    else { direct = T.keyLevel * T.balance; ambient = T.keyLevel * (1 - T.balance); }
    const key = direct + ambient;
    const zones = {};
    for (const [n, z] of Object.entries(st.zones)) {
      const wv = zoneWind(z, wind);
      zones[n] = { shelter: z.shelter, lightScale: z.lightScale, wetness: z.wetness, frost: z.frost, dust: z.dust, temperature: T.temperature + z.heat,
        wind: wv, windSpeed: Math.hypot(wv[0], wv[1]), rain: T.rainRate * (1 - z.shelter), light: key * z.lightScale + st.flash * 0.8 * (1 - z.shelter), dark: z.dark, wet: z.wet };
    }
    snap = deepFreeze({
      version: ENV_VERSION, step: st.step, clock: st.clock, slots: list.map((s) => s.name),
      time: { hour: st.hour, day: st.day, timeScale: st.timeScale, sunDir: sun, moonDir: moon, sunElevation: Math.asin(sun[1]) },
      light: { keyLevel: key, balance: key > 0 ? direct / key : 0, direct: { intensity: direct, colour: T.directColour, dir: sun[1] > 0 ? sun : moon },
        ambient: { intensity: ambient, sky: T.ambientSky, ground: T.ambientGround }, shadowStrength: T.shadowStrength, colourTemp: T.colourTemp, flash: st.flash },
      atmosphere: { fogDensity: T.fogDensity, fogFalloff: T.fogFalloff, fogColour: T.fogColour, anisotropy: T.anisotropy, cloudCover: T.cloudCover, haze: T.haze },
      weather: { weights: { ...st.weights }, target: st.target, rainRate: T.rainRate, snowRate: T.snowRate, raining: st.rainOn },
      wind: { dir: wind.dir, speed: wind.speed, gust: wind.gust, phase: st.clock * wind.speed },
      moisture: { humidity: T.humidity },
      temperature: { air: T.temperature },
      zones, events,
    });
    return snap;
  }

  return {
    seed, weathers, slots: list,
    command(cmd, { source = "unknown", reason = "" } = {}) {
      if (!PRIORITY.includes(cmd.type)) throw new Error(`environment: unknown command ${cmd.type}`);
      st.queue.push({ seq: st.seq++, cmd: structuredClone(cmd), source, reason });
    },
    step, snapshot,
    subscribe(f) { subs.add(f); return () => subs.delete(f); },
    get log() { return log.map((e) => ({ ...e })); },
    save() { return JSON.parse(JSON.stringify({ version: ENV_VERSION, config: { seed, slots: list, library, latitude, composition }, state: st, log })); },
  };
}

export function loadEnvironment(saved) {
  if (saved.version !== ENV_VERSION) throw new Error(`environment: cannot load ${saved.version}; this build reads ${ENV_VERSION}`);
  return createEnvironment(saved.config, { state: saved.state, log: saved.log });
}

function deepFreeze(o) { if (o && typeof o === "object" && !Object.isFrozen(o)) { Object.freeze(o); for (const v of Object.values(o)) deepFreeze(v); } return o; }
