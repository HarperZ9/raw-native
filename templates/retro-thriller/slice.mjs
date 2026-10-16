// Low Water, the ferry shelter: the retro-thriller template's vertical slice. A small original
// place where you walk, the weather turns, and light, surfaces, wind, sight and the rules of
// play all answer to one environment state (ADR 0017) on the G1 fixed step (ADR 0014).
//
// Play: web/game/play.html?scene=../../templates/retro-thriller/slice.mjs
//   WASD, arrows or a stick walk; click walks to a point; E (pad bottom button) opens and shuts
//   the door, or relights the lamp at the stove; T steps the weather (a test control); wheel zooms.
// The night: dusk under cloud, rain from 6 s, a storm from 16 s, fog from 30 s, clearing at 50 s,
// and round again every 90 s. The film plays the scripted walk below through the same game.
import { figureTopDown, walkToward } from "../common/actor.mjs";
import { wetSurface, rainItems, clothItems, plantItems, sightMask, shade } from "../common/env_view.mjs";
import { circle, rect, polyline } from "../../web/motion/path.mjs";
import { view2d, stepView2d, keepInside, toScene, toDesign, lerpView2d, motionCamera } from "../../web/game/cameras.mjs";
import { mix, mixAngle } from "../../web/game/loop.mjs";
import { frame as actionFrame } from "../../web/game/input.mjs";
import { filmFrame } from "../../web/game/film.mjs";
import { createEnvironment } from "../../web/env/state.mjs";
import { motionLook, wetMaterial, sway, stepSway, torch, stepTorch, sightRange } from "../../web/env/readers.mjs";
import { T, W, H, UNITS_PER_M, SHELTER, DOOR, STOVE, LAMP_POSTS, BOLLARDS, TARP, PLANT_IN, PLANT_OUT, BENCH, WATER_Y, solidAt, inShelter, tileset, map } from "./slice/level.mjs";
import { stepPrints, stepLamps, lampFlicker, doorCommand } from "./slice/rules.mjs";
import { hud } from "./slice/hud.mjs";

const HZ = 60, CYCLE = 90 * HZ;
const SCRIPT = [[0, "overcast", 0], [6, "rain", 6], [16, "storm", 4], [30, "fog", 8], [50, "clear", 15], [70, "overcast", 15]];
const DOOR_AT = [DOOR.x + 12, (DOOR.y0 + DOOR.y1) / 2];
const QUAY = [T, 5 * T, (W - 2) * T, 15 * T], WATER = [0, WATER_Y, W * T, H * T - WATER_Y];
const zoneAt = (p) => (inShelter(p) ? "shelter" : "outside");
const near = (p, q, r) => Math.hypot(p[0] - q[0], p[1] - q[1]) < r;

function walk(s, f, dt) {
  const speed = 95, r = 9, ax = f.axes.moveX || 0, ay = f.axes.moveY || 0, solid = (x, y) => solidAt(x, y, s.doorOpen);
  let goal = s.goal, moving = false, facing = s.facing, want = s.p;
  if (f.pressed.walkTo && f.pointer) goal = toScene(s.cam, f.pointer);
  if (ax || ay) { goal = null; const n = Math.max(1, Math.hypot(ax, ay)); want = [s.p[0] + (ax / n) * speed * dt, s.p[1] + (ay / n) * speed * dt]; }
  else if (goal) { const [q, done] = walkToward(s.p, goal, speed, dt); want = q; if (done) goal = null; }
  // Slide along walls and props: try each axis on its own (circle against solid cells and props).
  const blocked = (x, y) => solid(x + r, y) || solid(x - r, y) || solid(x, y + r) || solid(x, y - r);
  let [x, y] = s.p;
  if (!blocked(want[0], y)) x = want[0];
  if (!blocked(x, want[1])) y = want[1];
  if (x !== s.p[0] || y !== s.p[1]) { moving = true; facing = Math.atan2(y - s.p[1], x - s.p[0]); }
  else if (goal && (want[0] !== s.p[0] || want[1] !== s.p[1])) goal = null;   // walked into something: stop
  return { p: [x, y], goal, moving, facing };
}

function writeWeather(s, f, env) {
  const k = s.snap.step % CYCLE;
  for (const [at, state, seconds] of SCRIPT) if (k === at * HZ) env.command({ type: "setWeatherTarget", state, seconds }, { source: "low-water", reason: `the night turns: ${state}` });
  if (f.pressed.weather) { const ws = ["clear", "overcast", "rain", "storm", "fog"], w = ws[(ws.indexOf(s.snap.weather.target) + 1) % ws.length]; env.command({ type: "setWeatherTarget", state: w, seconds: 4 }, { source: "player", reason: "test control" }); }
}

export const game = {
  hz: HZ,
  actions: {
    moveX: { type: "axis", bind: ["key:KeyD", "-key:KeyA", "key:ArrowRight", "-key:ArrowLeft", "pad.axis:0"] },
    moveY: { type: "axis", bind: ["key:KeyS", "-key:KeyW", "key:ArrowDown", "-key:ArrowUp", "pad.axis:1"] },
    walkTo: { type: "button", bind: ["pointer:0"] },
    use: { type: "button", bind: ["key:KeyE", "pad.button:0"] },
    weather: { type: "button", bind: ["key:KeyT"] },
    zoom: { type: "axis", bind: ["wheel", "key:Minus", "-key:Equal", "pad.axis:3"] },
  },
  init(seed) {
    const env = createEnvironment({ seed, zones: { shelter: { shelter: 1, lightScale: 0.5 } }, start: { hour: 19.5, timeScale: 100, weather: "overcast" } });
    env.command({ type: "heat", zone: "shelter", degrees: 9, seconds: 20 }, { source: "low-water", reason: "the stove is lit" });
    const start = [7 * T, 9 * T];
    return { p: start, facing: 0, phase: 0, moving: false, goal: null, t: 0, doorOpen: false, env, snap: env.snapshot(),
      lamp: torch(), lampLight: 1, gutter: 0, prints: [], since: 0, posts: LAMP_POSTS.map(() => ({ on: false, level: 0, wait: 0 })),
      tarp: sway({ stiffness: 4, damping: 1.4, response: 0.1, axis: [1, 0] }), plantOut: sway({ stiffness: 8, damping: 2, response: 0.06 }), plantIn: sway({ stiffness: 8, damping: 2, response: 0.12 }),
      cam: view2d({ x: start[0], y: start[1], zoom: 2, minZoom: 1.2, maxZoom: 3.5 }) };
  },
  step(s, f, dt) {
    const m = walk(s, f, dt), env = s.env, zone = zoneAt(m.p);
    writeWeather(s, f, env);
    let doorOpen = s.doorOpen, relight = false;
    if (f.pressed.use && near(m.p, DOOR_AT, 60)) { doorOpen = !doorOpen; env.command(doorCommand(doorOpen), { source: "player", reason: doorOpen ? "opened the door" : "shut the door" }); }
    else if (f.pressed.use && near(m.p, STOVE, 64)) relight = true;
    const snap = env.step(dt), z = snap.zones[zone], gust = 0.6 * snap.wind.gust;
    const lamp = stepTorch(s.lamp, snap, zone, dt, { relight, dryRate: 0.25 });
    const pr = stepPrints(s.prints, { pos: m.p, facing: m.facing, moving: m.moving, wetness: z.wetness, open: 1 - z.shelter }, snap.weather.rainRate, dt, s.since);
    const cam = keepInside(stepView2d(s.cam, dt, { follow: m.p, followRate: 2.5, zoomSteps: f.axes.zoom || 0 }), [0, 0, W * T, H * T]);
    return { ...m, t: s.t + dt, phase: s.phase + (m.moving ? dt * 9.5 : 0), doorOpen, env, snap, lamp: lamp.state, lampLight: lamp.light, gutter: lamp.gutter,
      prints: pr.prints, since: pr.since, posts: stepLamps(s.posts, snap.zones.outside.dark, dt),
      tarp: stepSway(s.tarp, snap.zones.outside.wind, dt, gust), plantOut: stepSway(s.plantOut, snap.zones.outside.wind, dt, gust), plantIn: stepSway(s.plantIn, snap.zones.shelter.wind, dt), cam };
  },
  lerp: (a, b, k) => ({ ...b, p: [mix(a.p[0], b.p[0], k), mix(a.p[1], b.p[1], k)], facing: mixAngle(a.facing, b.facing, k), phase: mix(a.phase, b.phase, k), t: mix(a.t, b.t, k), cam: lerpView2d(a.cam, b.cam, k),
    tarp: { ...b.tarp, angle: mix(a.tarp.angle, b.tarp.angle, k) }, plantOut: { ...b.plantOut, angle: mix(a.plantOut.angle, b.plantOut.angle, k) }, plantIn: { ...b.plantIn, angle: mix(a.plantIn.angle, b.plantIn.angle, k) } }),
  // The scripted walk for the film: to the door, out into the rain, down to the water, back in
  // when the lamp drowns, shut the door, relight at the stove, open the door on the fog.
  demo(i, mp, s) {
    const go = (x, y) => actionFrame(mp, { pressed: ["walkTo"], pointer: toDesign(s.cam, [x, y]) });
    const use = () => actionFrame(mp, { pressed: ["use"] });
    const plan = { 60: () => go(392, 252), 240: use, 270: () => go(640, 300), 480: () => go(880, 440), 720: () => go(1000, 300),
      1260: () => go(392, 252), 1710: use, 1740: () => go(150, 222), 1990: use, 2040: () => go(392, 252), 2190: use, 2250: () => go(640, 320) };
    return plan[i] ? plan[i]() : actionFrame(mp, {});
  },
  view(s, A) { return { background: [0.004, 0.004, 0.006], camera: motionCamera(s.cam), items: drawPlace(s, A).filter(Boolean), post: drawPost(s) }; },
};

function drawPlace(s, A) {
  const snap = s.snap, t = s.t, quay = motionLook(snap, "outside"), room = motionLook(snap, "shelter"), zone = zoneAt(s.p);
  const stoveFlick = 0.85 + 0.1 * Math.sin(t * 9.1) + 0.05 * Math.sin(t * 23.7), stove = [1.0 * stoveFlick, 0.55 * stoveFlick, 0.25 * stoveFlick];
  const posts = LAMP_POSTS.map((p, i) => ({ x: p[0], y: p[1], level: lampFlicker(s.posts[i].level, t, i), colour: [0.75, 0.82, 1] }));
  const mat = wetMaterial({ albedo: [0.24, 0.25, 0.26], roughness: 0.7, porosity: 0.8 }, snap.zones.outside.wetness);
  const water = wetMaterial({ albedo: [0.06, 0.12, 0.15], roughness: 0.3, porosity: 0 }, 1);
  const [px, py] = s.p, g = 1 + 0.35 * s.gutter * Math.sin(t * 23) * Math.sin(t * 7.3), hl = 0.4 * s.lampLight * g;
  const practical = Math.max(0, 1 - Math.hypot(px - STOVE[0], py - STOVE[1]) / 220) * 5 + Math.max(...posts.map((L) => L.level * Math.max(0, 1 - Math.hypot(px - L.x, py - L.y) / 170) * 4));
  const sight = sightRange(snap, zone, { clearRange: 40, carried: Math.max(s.lamp.lit ? 4 : 0, practical) }) * UNITS_PER_M;
  const glow = (x, y, r, c, a = 1) => ({ shape: circle(x, y, r, 72), fill: { radial: [x, y, r], stops: [[0, [c[0] * a, c[1] * a, c[2] * a, 1]], [1, [0, 0, 0, 0]]] }, blend: "add" });
  const fog = quay.fog;
  return [
    { kind: "tilemap", map: A.map, tileset: "low-water-set", x: 0, y: 0, scale: 1, opacity: Math.min(0.95, 0.3 + 0.6 * quay.brightness) },
    shade(SHELTER, 1 - Math.min(1, room.brightness / Math.max(0.05, quay.brightness)) * 0.85),
    ...wetSurface(QUAY, mat, posts), ...wetSurface(WATER, water, posts.map((L) => ({ ...L, y: WATER_Y })).concat([{ x: STOVE[0], y: WATER_Y, colour: stove, level: s.doorOpen ? 0.2 : 0 }])),
    ...waterItems(t, snap), ...s.prints.map((p) => ({ shape: circle(p.x, p.y, 3.2, 10), fill: [0, 0, 0, 0.45 * p.s] })),
    { shape: rect(BENCH[0] * T, BENCH[1] * T + 4, BENCH[2] * T, T - 8, 3), fill: "#3a2a20" },
    { shape: circle(STOVE[0], STOVE[1], 18, 32), fill: "#1b1b1d", stroke: "#3a3a3e", width: 3 },
    s.doorOpen ? { shape: rect(DOOR.x + 2, DOOR.y0 - 30, 6, 72), fill: "#4a3527" } : { shape: rect(DOOR.x + 4, DOOR.y0, 16, DOOR.y1 - DOOR.y0), fill: "#4a3527" },
    ...BOLLARDS.map((b) => ({ shape: circle(b[0], b[1], 8, 20), fill: "#202224", stroke: "#4a4c4e", width: 2 })),
    ...LAMP_POSTS.map((p) => ({ shape: circle(p[0], p[1], 6, 16), fill: "#2c2e30" })),
    ...plantItems(PLANT_IN, s.plantIn.angle, { colour: "#35603f" }), ...plantItems(PLANT_OUT, s.plantOut.angle),
    ...clothItems([TARP[0] + 40, TARP[1]], { angle: s.tarp.angle, width: 80, length: 110, colour: "#2f3b4a" }),
    ...figureTopDown(s.p, s.facing, s.phase, { scale: 1.9, stride: s.moving ? 1 : 0, coat: "#4a3f36" }),
    glow(STOVE[0], STOVE[1], 230, stove, 0.55), ...posts.map((L) => (L.level > 0.01 ? glow(L.x, L.y, 170, L.colour, 0.5 * L.level) : null)),
    // Fog scatters lamplight: a wide, soft halo grows round each lit lamp as the fog thickens.
    ...posts.map((L) => (L.level > 0.01 && fog.amount > 0.05 ? glow(L.x, L.y, 170 + 420 * fog.amount, [0.55, 0.6, 0.7], 0.5 * L.level * fog.amount) : null)),
    fog.amount > 0.05 && s.doorOpen ? glow(DOOR_AT[0] + 30, DOOR_AT[1], 120 + 300 * fog.amount, [1, 0.6, 0.3], 0.35 * fog.amount) : null,
    hl > 0.01 ? glow(px + Math.cos(s.facing) * 20, py + Math.sin(s.facing) * 20, 70, [1, 0.8, 0.5], hl) : null,
    ...rainItems(QUAY, snap.zones.outside.rain, snap.zones.outside.wind, t), ...rainItems(WATER, snap.zones.outside.rain * 0.6, snap.zones.outside.wind, t, 12),
    s.doorOpen ? rainItems([DOOR.x - 72, DOOR.y0, 72, DOOR.y1 - DOOR.y0], snap.zones.shelter.rain, snap.zones.shelter.wind, t, 13) : [],
    quay.flash > 0.02 ? { shape: rect(0, 0, W * T, H * T), fill: [0.8, 0.85, 1, 0.3 * quay.flash], blend: "add" } : null,
    // Fog lies over the quay and the water, thicker toward the sea, and stays out of the room.
    { shape: rect(0, 4 * T, W * T, H * T), fill: { linear: [0, 5 * T, 0, H * T], stops: [[0, [...fog.colour, fog.amount * 0.35]], [1, [...fog.colour, fog.amount * 0.75]]] },
      clip: [...rect(0, 0, W * T, H * T), ...rect(...SHELTER)], clipRule: "evenodd" },
    sightMask(s.p, sight),
    ...hud(snap, zone, { lampLit: s.lamp.lit, soak: s.lamp.soak }, A.atlas),
  ].flat();
}

// The harbour: slow swell lines that drift with the wind, brighter where a lamp lies on them.
function waterItems(t, snap) {
  const w = snap.zones.outside.wind, out = [], chop = Math.min(1, Math.hypot(w[0], w[1]) / 12);
  for (let k = 0; k < 18; k++) {
    const y = WATER_Y + 14 + k * 9, phase = t * (0.4 + 0.5 * chop) * (k % 2 ? 1 : -1) + k * 1.7, pts = [];
    for (let x = 0; x <= W * T; x += 48) pts.push(x, y + Math.sin(x / (60 - 30 * chop) + phase) * (1.5 + 3 * chop));
    out.push({ shape: polyline(pts), fill: null, stroke: [0.5, 0.6, 0.7, 0.07 + 0.08 * chop], width: 1.2 });
  }
  return out;
}

function drawPost(s) {
  const look = motionLook(s.snap, zoneAt(s.p));
  return { bloom: look.post.bloom + 0.15, threshold: 1.0, vignette: look.post.vignette, grain: 0, colour: undefined,
    passes: [{ pass: "film", preset: "500t-print" }, { pass: "crt-classic", cell: 3, scanlines: true, scanStrength: 0.22, mask: "grille", maskStrength: 0.12, bloom: 0.1, curvature: 0.05, aberration: 0.06, vignette: 0.3 }] };
}

export default {
  title: "Low Water: the ferry shelter (retro-thriller vertical slice)",
  duration: 40,
  fps: 30,
  game,
  async load(ctx) {
    if (ctx.motion) ctx.motion.image("low-water-set", tileset());
    let atlas = null;
    if (ctx.json) { try { atlas = await ctx.json("../../docs/motion/hanken-grotesk-500.atlas.json"); } catch (e) { console.warn("low water: no text atlas, the instruments draw without labels", e); } }
    return { map: map(true), atlas };
  },
  frame(t, ctx) { return filmFrame(game, ctx, t); },
};
