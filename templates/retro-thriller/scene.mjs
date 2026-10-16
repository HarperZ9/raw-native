// Template (c), ADR 0016: a thriller with a retro look, "more like Disco Elysium and
// less like horror ... I want it to be scary ... not ... focused entirely on horror and
// on jump scares and fear". Scaffold: placeholder art from code. A dim corridor seen from
// above, one unsteady practical light, colour negative film for the stock and a soft
// tube for the screen it plays on. Dread comes from the light and the framing, not shocks.
//
// Playable (runtime G1): walk with WASD, the arrows or a gamepad stick; click to walk to a
// point; the wheel or + and - zoom; E (or the pad's bottom button) relights the lamp under a
// roof; T steps the weather (a test control). Live: web/game/play.html?scene=
// ../../templates/retro-thriller/scene.mjs. The film and the CI check play the scripted demo.
//
// The environment state (ADR 0017) runs the place. The corridor has a roof; the courtyard
// between columns 34 and 44 is open to the sky. Stepping into the courtyard the first time
// brings the storm in (a gameplay writer). The storm darkens the look, wets the courtyard
// floor (darker, with lamp reflections), drops rain, sways the hanging cloth and the plants,
// and soaks the hand lamp until it goes out. Thunder rattles the corridor door, so the plant
// inside sways too. Fog and darkness close the walker's sight.
import { makeTileset, makeLevel, tileMap, shade as tint } from "../common/procedural.mjs";
import { figureTopDown, walkToward } from "../common/actor.mjs";
import { shade, wetSurface, rainItems, clothItems, plantItems, sightMask } from "../common/env_view.mjs";
import { circle, rect } from "../../web/motion/path.mjs";
import { view2d, stepView2d, keepInside, toScene, lerpView2d, motionCamera } from "../../web/game/cameras.mjs";
import { mix, mixAngle } from "../../web/game/loop.mjs";
import { frame as actionFrame } from "../../web/game/input.mjs";
import { filmFrame } from "../../web/game/film.mjs";
import { createEnvironment } from "../../web/env/state.mjs";
import { motionLook, wetMaterial, sway, stepSway, torch, stepTorch, sightRange } from "../../web/env/readers.mjs";

const T = 24, W = 48, H = 28, UNITS_PER_M = 48;
const KINDS = ["#2a2f36", "#3c3a35", "#14161a", "#34383a"];      // floor, carpet, wall, courtyard flags
const wallRow = (y) => y < 6 || y > 21;
const COURT = [34 * T, 6 * T, 10 * T, 16 * T];                     // x, y, w, h of the open courtyard
const LAMP = [600, 330], POST = [936, 250];
const zoneAt = ([x]) => (x >= COURT[0] && x < COURT[0] + COURT[2] ? "outside" : "corridor");
const WEATHERS = ["clear", "overcast", "rain", "storm", "fog"];

function moveWalker(s, f, dt) {
  const speed = 90, r = 8, ax = f.axes.moveX || 0, ay = f.axes.moveY || 0;
  let p = s.p, goal = s.goal, moving = false, facing = s.facing;
  if (f.pressed.walkTo && f.pointer) goal = toScene(s.cam, f.pointer);
  if (ax || ay) {
    goal = null;
    const n = Math.max(1, Math.hypot(ax, ay));
    p = [p[0] + (ax / n) * speed * dt, p[1] + (ay / n) * speed * dt];
    facing = Math.atan2(ay, ax); moving = true;
  } else if (goal) {
    const [q, done] = walkToward(p, goal, speed, dt);
    if (Math.hypot(q[0] - p[0], q[1] - p[1]) > 0) facing = Math.atan2(q[1] - p[1], q[0] - p[0]);
    p = q; moving = !done; if (done) goal = null;
  }
  // The corridor's walls bound the walk (rows 6 to 21 are floor), and the map's ends.
  const y = Math.min(22 * T - r, Math.max(6 * T + r, p[1])), x = Math.min(W * T - r, Math.max(r, p[0]));
  if (goal && (y !== p[1] || x !== p[0])) goal = null;
  return { p: [x, y], goal, moving, facing };
}

// Gameplay writers: the first step into the courtyard brings the storm; it eases to rain and
// then to overcast on a timer; thunder rattles the corridor door (a gust inside); T steps the weather.
function writeEnvironment(s, f, env, zone) {
  const cmd = (c, reason) => env.command(c, { source: "retro-thriller", reason });
  let { stormAt } = s;
  if (zone === "outside" && stormAt === null) { cmd({ type: "setWeatherTarget", state: "storm", seconds: 3 }, "the walker stepped into the courtyard: storm starts"); stormAt = s.snap.step; }
  if (stormAt !== null && s.snap.step - stormAt === 25 * 60) cmd({ type: "setWeatherTarget", state: "rain", seconds: 10 }, "the storm eases");
  if (stormAt !== null && s.snap.step - stormAt === 60 * 60) cmd({ type: "setWeatherTarget", state: "overcast", seconds: 20 }, "the rain stops");
  for (const e of s.snap.events) if (e.type === "thunder") cmd({ type: "addWind", zone: "corridor", vector: [4, 0.5], seconds: 1.5 }, "thunder rattles the door");
  if (f.pressed.weather) { const w = WEATHERS[(WEATHERS.indexOf(s.snap.weather.target) + 1) % WEATHERS.length]; cmd({ type: "setWeatherTarget", state: w, seconds: 4 }, "test control"); }
  return stormAt;
}

export const game = {
  hz: 60,
  actions: {
    moveX: { type: "axis", bind: ["key:KeyD", "-key:KeyA", "key:ArrowRight", "-key:ArrowLeft", "pad.axis:0"] },
    moveY: { type: "axis", bind: ["key:KeyS", "-key:KeyW", "key:ArrowDown", "-key:ArrowUp", "pad.axis:1"] },
    walkTo: { type: "button", bind: ["pointer:0"] },
    use: { type: "button", bind: ["key:KeyE", "pad.button:0"] },
    weather: { type: "button", bind: ["key:KeyT"] },
    zoom: { type: "axis", bind: ["wheel", "key:Minus", "-key:Equal", "pad.axis:3"] },
  },
  init(seed) {
    const env = createEnvironment({ seed, zones: { corridor: { shelter: 1, lightScale: 0.55 } }, start: { hour: 20.25, timeScale: 90, weather: "overcast" } });
    return { p: [520, 336], facing: 0, phase: 0, moving: false, goal: null, t: 0, stormAt: null, env, snap: env.snapshot(), lamp: torch(), lampLight: 1, gutter: 0,
      cloth: sway({ stiffness: 5, damping: 1.6, response: 0.09, axis: [1, 0] }), plantOut: sway({ stiffness: 9, damping: 2, response: 0.05, axis: [1, 0] }), plantIn: sway({ stiffness: 9, damping: 2, response: 0.08, axis: [1, 0] }),
      cam: view2d({ x: 436, y: 336, zoom: 2.2, minZoom: 1.4, maxZoom: 3.5 }) };
  },
  step(s, f, dt) {
    const m = moveWalker(s, f, dt), zone = zoneAt(m.p), env = s.env;
    const stormAt = writeEnvironment(s, f, env, zone);
    const snap = env.step(dt), out = snap.zones.outside.wind, inn = snap.zones.corridor.wind, flutter = 0.6 * snap.wind.gust;
    const lit = stepTorch(s.lamp, snap, zone, dt, { relight: !!f.pressed.use, dryRate: 0.25 });
    const cam = keepInside(stepView2d(s.cam, dt, { follow: m.p, followRate: 3, zoomSteps: f.axes.zoom || 0 }), [0, 0, W * T, H * T]);
    return { ...m, t: s.t + dt, phase: s.phase + (m.moving ? dt * 9 : 0), stormAt, env, snap, lamp: lit.state, lampLight: lit.light, gutter: lit.gutter,
      cloth: stepSway(s.cloth, out, dt, flutter), plantOut: stepSway(s.plantOut, out, dt, flutter), plantIn: stepSway(s.plantIn, inn, dt), cam };
  },
  lerp: (a, b, k) => ({ ...b, p: [mix(a.p[0], b.p[0], k), mix(a.p[1], b.p[1], k)], facing: mixAngle(a.facing, b.facing, k), phase: mix(a.phase, b.phase, k), cam: lerpView2d(a.cam, b.cam, k), t: mix(a.t, b.t, k),
    cloth: { ...b.cloth, angle: mix(a.cloth.angle, b.cloth.angle, k) }, plantOut: { ...b.plantOut, angle: mix(a.plantOut.angle, b.plantOut.angle, k) }, plantIn: { ...b.plantIn, angle: mix(a.plantIn.angle, b.plantIn.angle, k) } }),
  // The scripted demo: walk east under the lamp into the courtyard (the storm comes), stand in
  // the rain until the lamp drowns, go back under the roof, relight it, lean in.
  demo(i, map) {
    const t = i / 60;
    if (t >= 0.5 && t < 3.5) return actionFrame(map, { axes: { moveX: 1 } });
    if (t >= 3.5 && t < 5) return actionFrame(map, { axes: { moveX: 1, moveY: -0.3 } });
    if (t >= 7 && t < 8) return actionFrame(map, { axes: { moveY: 0.6 } });
    if (t >= 12 && t < 14.5) return actionFrame(map, { axes: { moveX: -1 } });
    if (i === 1080) return actionFrame(map, { pressed: ["use"] });
    if (i === 1140 || i === 1152) return actionFrame(map, { axes: { zoom: -1 } });
    return actionFrame(map, {});
  },
  view(s, A) {
    const t = s.t, snap = s.snap, zone = zoneAt(s.p), look = motionLook(snap, "corridor"), court = motionLook(snap, "outside");
    // The corridor lamp flickers: a slow sway plus two fast terms, never a strobe.
    const flick = 0.8 + 0.12 * Math.sin(t * 1.3) + 0.05 * Math.sin(t * 17.0) + 0.03 * Math.sin(t * 29.0);
    const lamp = [0.9 * flick, 0.65 * flick, 0.4 * flick, 1], post = [0.75, 0.8, 0.95];
    const [px, py] = s.p, ahead = [px + Math.cos(s.facing) * 22, py + Math.sin(s.facing) * 22];
    const g = 1 + 0.35 * s.gutter * Math.sin(t * 23) * Math.sin(t * 7.3), hl = 0.35 * s.lampLight * g;
    const mat = wetMaterial({ albedo: [0.2, 0.22, 0.23], roughness: 0.7, porosity: 0.8 }, snap.zones.outside.wetness);
    const sight = sightRange(snap, zone, { clearRange: 40, carried: s.lamp.lit ? 4 : 0 }) * UNITS_PER_M;
    const fog = look.fog, items = [
      { kind: "tilemap", map: A.map, tileset: "thriller-set", x: 0, y: 0, scale: 1, opacity: Math.min(0.95, 0.35 + 0.6 * look.brightness) },
      shade(COURT, 1 - Math.min(1, court.brightness / Math.max(0.05, look.brightness)) * 0.9),
      ...wetSurface(COURT, mat, [{ x: POST[0], y: POST[1], colour: post, level: 1 }, { x: COURT[0] + 2, y: LAMP[1], colour: lamp, level: 0.4 }]),
      { shape: circle(LAMP[0], LAMP[1], 260, 96), fill: { radial: [LAMP[0], LAMP[1], 260], stops: [[0, lamp], [1, [0, 0, 0, 0]]] }, blend: "add" },
      { shape: circle(POST[0], POST[1], 180, 72), fill: { radial: [POST[0], POST[1], 180], stops: [[0, [...post.map((c) => c * 0.45), 1]], [1, [0, 0, 0, 0]]] }, blend: "add" },
      ...plantItems([1020, 480], s.plantOut.angle), ...plantItems([770, 190], s.plantIn.angle, { colour: "#35603f" }),
      ...clothItems([936, 150], { angle: s.cloth.angle }),
      ...figureTopDown(s.p, s.facing, s.phase, { scale: 1.9, stride: s.moving ? 1 : 0, coat: "#4a3f36" }),
      hl > 0.01 ? { shape: circle(ahead[0], ahead[1], 70, 48), fill: { radial: [ahead[0], ahead[1], 70], stops: [[0, [hl, hl * 0.8, hl * 0.5, 1]], [1, [0, 0, 0, 0]]] }, blend: "add" } : null,
      ...rainItems(COURT, snap.zones.outside.rain, snap.zones.outside.wind, t),
      court.flash > 0.02 ? { shape: rect(...COURT), fill: [0.8, 0.85, 1, 0.5 * court.flash], blend: "add" } : null,
      { shape: rect(0, 0, W * T, H * T), fill: [...fog.colour, fog.amount * 0.55] },
      { shape: rect(0, 0, W * T, H * T), fill: [...look.tint, 0.08] },
      sightMask(s.p, sight),
    ];
    return {
      background: [0.004, 0.004, 0.006],
      camera: motionCamera(s.cam),
      items: items.filter(Boolean),
      post: { bloom: look.post.bloom + 0.15, threshold: 1.0, vignette: look.post.vignette, grain: 0, colour: undefined,
        passes: [{ pass: "film", preset: "500t-print" }, { pass: "crt-classic", cell: 3, scanlines: true, scanStrength: 0.25, mask: "grille", maskStrength: 0.15, bloom: 0.1, curvature: 0.06, aberration: 0.08, vignette: 0.35 }] },
    };
  },
};

export default {
  title: "Retro thriller (template scaffold, playable)",
  duration: 20,
  fps: 30,
  game,
  async load(ctx) {
    const set = makeTileset({ tileW: T, tileH: T, kinds: 4, seed: "thriller-set",
      kind: (k, v, x, y, n) => [...tint(KINDS[k], ((k === 0 || k === 3) && (x === 0 || y === 0) ? 0.7 : 1) * (0.85 + 0.3 * n)), 255] });
    if (ctx.motion) ctx.motion.image("thriller-set", set);
    const lv = makeLevel({ w: W, h: H, seed: "thriller-level", kindOf: (f, x, y) => (wallRow(y) ? 2 : x >= 34 && x < 44 ? 3 : x > 18 && x < 30 ? 1 : 0) });
    return { map: tileMap({ w: W, h: H, tileW: T, tileH: T, gids: lv.gids, columns: 8, kinds: 4 }) };
  },
  frame(t, ctx) { return filmFrame(game, ctx, t); },
};
