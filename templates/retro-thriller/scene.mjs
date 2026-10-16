// Template (c), ADR 0016: a thriller with a retro look, "more like Disco Elysium and
// less like horror ... I want it to be scary ... not ... focused entirely on horror and
// on jump scares and fear". Scaffold: placeholder art from code. A dim corridor seen from
// above, one unsteady practical light, colour negative film for the stock and a soft
// tube for the screen it plays on. Dread comes from the light and the framing, not shocks.
//
// Playable (runtime G1): walk with WASD, the arrows or a gamepad stick; click to walk to a
// point; the wheel or + and - zoom. The camera follows. Live: web/game/play.html?scene=
// ../../templates/retro-thriller/scene.mjs. The film and the CI check play the scripted demo.
import { makeTileset, makeLevel, tileMap, shade } from "../common/procedural.mjs";
import { figureTopDown, walkToward } from "../common/actor.mjs";
import { circle } from "../../web/motion/path.mjs";
import { view2d, stepView2d, keepInside, toScene, lerpView2d, motionCamera } from "../../web/game/cameras.mjs";
import { mix, mixAngle } from "../../web/game/loop.mjs";
import { frame as actionFrame } from "../../web/game/input.mjs";
import { filmFrame } from "../../web/game/film.mjs";

const T = 24, W = 48, H = 28;
const KINDS = ["#2a2f36", "#3c3a35", "#14161a"];                  // floor, carpet, wall
const wallRow = (y) => y < 6 || y > 21;
const LAMP = [600, 330];

export const game = {
  hz: 60,
  actions: {
    moveX: { type: "axis", bind: ["key:KeyD", "-key:KeyA", "key:ArrowRight", "-key:ArrowLeft", "pad.axis:0"] },
    moveY: { type: "axis", bind: ["key:KeyS", "-key:KeyW", "key:ArrowDown", "-key:ArrowUp", "pad.axis:1"] },
    walkTo: { type: "button", bind: ["pointer:0"] },
    zoom: { type: "axis", bind: ["wheel", "key:Minus", "-key:Equal", "pad.axis:3"] },
  },
  init: () => ({ p: [180, 336], facing: 0, phase: 0, moving: false, goal: null, cam: view2d({ x: 436, y: 336, zoom: 2.2, minZoom: 1.4, maxZoom: 3.5 }), t: 0 }),
  step(s, f, dt) {
    const speed = 90, r = 8;
    let p = s.p, goal = s.goal, moving = false, facing = s.facing;
    const ax = f.axes.moveX || 0, ay = f.axes.moveY || 0;
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
    const clampY = Math.min(22 * T - r, Math.max(6 * T + r, p[1])), clampX = Math.min(W * T - r, Math.max(r, p[0]));
    if (goal && (clampY !== p[1] || clampX !== p[0])) goal = null;
    p = [clampX, clampY];
    const cam = keepInside(stepView2d(s.cam, dt, { follow: p, followRate: 3, zoomSteps: f.axes.zoom || 0 }), [0, 0, W * T, H * T]);
    return { p, facing, phase: s.phase + (moving ? dt * 9 : 0), moving, goal, cam, t: s.t + dt };
  },
  lerp: (a, b, k) => ({ ...b, p: [mix(a.p[0], b.p[0], k), mix(a.p[1], b.p[1], k)], facing: mixAngle(a.facing, b.facing, k), phase: mix(a.phase, b.phase, k), cam: lerpView2d(a.cam, b.cam, k), t: mix(a.t, b.t, k) }),
  // The scripted demo: walk east under the lamp, step toward the wall, click ahead, come back, lean in.
  demo(i, map) {
    const t = i / 60;
    if (t >= 1 && t < 4) return actionFrame(map, { axes: { moveX: 1 } });
    if (t >= 4 && t < 4.8) return actionFrame(map, { axes: { moveY: -0.7, moveX: 0.4 } });
    if (i === 312) return actionFrame(map, { pressed: ["walkTo"], pointer: [1500, 760] });
    if (t >= 8 && t < 10) return actionFrame(map, { axes: { moveX: -1, moveY: 0.25 } });
    if (i === 630 || i === 642) return actionFrame(map, { axes: { zoom: -1 } });
    return actionFrame(map, {});
  },
  view(s, A) {
    const t = s.t;
    // The lamp flickers: a slow sway plus two fast terms, never a strobe.
    const flick = 0.8 + 0.12 * Math.sin(t * 1.3) + 0.05 * Math.sin(t * 17.0) + 0.03 * Math.sin(t * 29.0);
    const lamp = [0.9 * flick, 0.65 * flick, 0.4 * flick, 1];
    const [px, py] = s.p, ahead = [px + Math.cos(s.facing) * 22, py + Math.sin(s.facing) * 22];
    return {
      background: [0.004, 0.004, 0.006],
      camera: motionCamera(s.cam),
      items: [
        { kind: "tilemap", map: A.map, tileset: "thriller-set", x: 0, y: 0, scale: 1, opacity: 0.9 },
        { shape: circle(LAMP[0], LAMP[1], 260, 96), fill: { radial: [LAMP[0], LAMP[1], 260], stops: [[0, lamp], [1, [0, 0, 0, 0]]] }, blend: "add" },
        ...figureTopDown(s.p, s.facing, s.phase, { scale: 1.9, stride: s.moving ? 1 : 0, coat: "#4a3f36" }),
        // A hand torch: a weak pool of warm light just ahead of the walker.
        { shape: circle(ahead[0], ahead[1], 70, 48), fill: { radial: [ahead[0], ahead[1], 70], stops: [[0, [0.35, 0.28, 0.18, 1]], [1, [0, 0, 0, 0]]] }, blend: "add" },
      ],
      post: { bloom: 0.25, threshold: 1.0, vignette: 0.5, grain: 0, colour: undefined,
        passes: [{ pass: "film", preset: "500t-print" }, { pass: "crt-classic", cell: 3, scanlines: true, scanStrength: 0.25, mask: "grille", maskStrength: 0.15, bloom: 0.1, curvature: 0.06, aberration: 0.08, vignette: 0.35 }] },
    };
  },
};

export default {
  title: "Retro thriller (template scaffold, playable)",
  duration: 12,
  fps: 30,
  game,
  async load(ctx) {
    const set = makeTileset({ tileW: T, tileH: T, kinds: 3, seed: "thriller-set",
      kind: (k, v, x, y, n) => [...shade(KINDS[k], (k === 0 && (x === 0 || y === 0) ? 0.7 : 1) * (0.85 + 0.3 * n)), 255] });
    if (ctx.motion) ctx.motion.image("thriller-set", set);
    const lv = makeLevel({ w: W, h: H, seed: "thriller-level", kindOf: (f, x, y) => (wallRow(y) ? 2 : x > 18 && x < 30 ? 1 : 0) });
    return { map: tileMap({ w: W, h: H, tileW: T, tileH: T, gids: lv.gids, columns: 8, kinds: 3 }) };
  },
  frame(t, ctx) { return filmFrame(game, ctx, t); },
};
