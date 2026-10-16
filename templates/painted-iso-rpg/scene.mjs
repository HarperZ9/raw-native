// Template (a), ADR 0016: a painted isometric RPG, "more Disco Elysium and less Rogue
// Trader", "very, very, very abstract, artistic, grotesque", "very, very indie", "make it
// real funky". Scaffold: placeholder art from code. An isometric district of blocks in
// scene light; the painterly pass turns it to thick, outlined oil when it is registered
// (raw-native PR #55), and colour negative film stands in until then.
//
// Playable (runtime G1): the cursor picks the cell under the pointer; click to walk there by
// the shortest path around the blocks; WASD, the arrows or a stick walk directly; the wheel
// zooms about the pointer. Live: web/game/play.html?scene=../../templates/painted-iso-rpg/scene.mjs.
import { makeTileset, makeLevel, tileMap, shade } from "../common/procedural.mjs";
import { figureIso, walkToward, gridPath } from "../common/actor.mjs";
import { registeredPasses } from "../../web/motion/post.mjs";
import { polyline } from "../../web/motion/path.mjs";
import { isoCamera } from "../../web/world/iso.mjs";
import { view2d, stepView2d, toScene, lerpView2d, motionCamera } from "../../web/game/cameras.mjs";
import { mix } from "../../web/game/loop.mjs";
import { frame as actionFrame } from "../../web/game/input.mjs";
import { filmFrame } from "../../web/game/film.mjs";

const TW = 64, TH = 32, N = 24, ORIGIN = [960, 140];
const KINDS = ["#a0704a", "#c4503a", "#5f9a74", "#e8b84a"];       // earth, brick, moss, ochre
const ISO = isoCamera({ tileW: TW, tileH: TH, origin: ORIGIN, width: N, height: N });
const walkable = (A, i, j) => i >= 0 && j >= 0 && i < N && j < N && A.kinds[j * N + i] < 2;   // moss and ochre are raised blocks
const cellOf = ([ci, cj]) => [Math.floor(ci), Math.floor(cj)];

export const game = {
  hz: 60,
  actions: {
    moveX: { type: "axis", bind: ["key:KeyD", "-key:KeyA", "key:ArrowRight", "-key:ArrowLeft", "pad.axis:0"] },
    moveY: { type: "axis", bind: ["key:KeyS", "-key:KeyW", "key:ArrowDown", "-key:ArrowUp", "pad.axis:1"] },
    walkTo: { type: "button", bind: ["pointer:0", "pad.button:0"] },
    zoom: { type: "axis", bind: ["wheel", "key:Minus", "-key:Equal"] },
  },
  init(seed, A) {
    // Start on the walkable cell nearest the map's centre (a fixed scan order, so it is deterministic).
    let best = null;
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) if (walkable(A, i, j)) { const d = (i - 11.5) ** 2 + (j - 11.5) ** 2; if (!best || d < best.d) best = { i, j, d }; }
    const c = [best.i + 0.5, best.j + 0.5], [sx, sy] = ISO.toScreen(...c);
    return { c, path: [], hover: null, phase: 0, moving: false, cam: view2d({ x: sx, y: sy, zoom: 1.4, minZoom: 0.7, maxZoom: 2.5 }), t: 0 };
  },
  step(s, f, dt, A) {
    let c = s.c, path = s.path, moving = false;
    const speed = 2.6;                                              // cells per second
    const hover = f.pointer ? ISO.pick(...toScene(s.cam, f.pointer)) : s.hover;
    if (f.pressed.walkTo && hover && walkable(A, ...hover)) path = gridPath(N, N, (i, j) => walkable(A, i, j), cellOf(c), hover) || [];
    const ax = f.axes.moveX || 0, ay = f.axes.moveY || 0;
    if (ax || ay) {
      // Screen directions on the diamond: right is +i -j, down is +i +j.
      path = [];
      const di = (ax + ay) / Math.SQRT2, dj = (ay - ax) / Math.SQRT2, n = Math.max(1, Math.hypot(di, dj));
      const next = [c[0] + (di / n) * speed * dt, c[1] + (dj / n) * speed * dt];
      // Slide along blocks: try both axes, then each alone.
      for (const q of [next, [next[0], c[1]], [c[0], next[1]]]) if (walkable(A, ...cellOf(q))) { c = q; moving = true; break; }
    } else if (path.length) {
      const [q, arrived] = walkToward(c, [path[0][0] + 0.5, path[0][1] + 0.5], speed, dt);
      c = q; moving = true;
      if (arrived) path = path.slice(1);
    }
    const feet = ISO.toScreen(...c);
    const cam = stepView2d(s.cam, dt, { follow: feet, followRate: 2.5, zoomSteps: f.axes.zoom || 0, anchor: f.pointer });
    return { c, path, hover, phase: s.phase + (moving ? dt * 10 : 0), moving, cam, t: s.t + dt };
  },
  lerp: (a, b, k) => ({ ...b, c: [mix(a.c[0], b.c[0], k), mix(a.c[1], b.c[1], k)], phase: mix(a.phase, b.phase, k), cam: lerpView2d(a.cam, b.cam, k), t: mix(a.t, b.t, k) }),
  // The scripted demo: hover across the district, click a far cell, then walk by stick, then zoom.
  demo(i, map) {
    const t = i / 60, sweep = [700 + 500 * Math.min(1, t / 3), 420 + 160 * Math.sin(t * 0.9)];
    if (i === 150) return actionFrame(map, { pressed: ["walkTo"], pointer: [1300, 700] });
    if (t < 6) return actionFrame(map, { pointer: sweep });
    if (t >= 7 && t < 9.5) return actionFrame(map, { axes: { moveX: -0.8, moveY: -0.4 } });
    if (i === 600 || i === 615) return actionFrame(map, { axes: { zoom: -1 }, pointer: [960, 540] });
    return actionFrame(map, {});
  },
  view(s, A) {
    const paint = registeredPasses().includes("paint");
    const diamond = ([i, j], lift = 0) => polyline([...ISO.toScreen(i, j), ...ISO.toScreen(i + 1, j), ...ISO.toScreen(i + 1, j + 1), ...ISO.toScreen(i, j + 1)].map((v, k) => (k % 2 ? v - lift : v)), true);
    const items = [{ kind: "tilemap", map: A.map, tileset: "iso-set", x: ORIGIN[0], y: ORIGIN[1], scale: 1 }];
    for (const [k, cell] of s.path.entries()) items.push({ shape: diamond(cell), fill: [0.98, 0.92, 0.7, 0.28 + 0.08 * (k % 2)], stroke: null });
    if (s.hover) items.push({ shape: diamond(s.hover), fill: walkable(A, ...s.hover) ? [1, 0.96, 0.85, 0.3] : [0.5, 0.05, 0.05, 0.35], stroke: walkable(A, ...s.hover) ? "#fff4dc" : "#3a0808", width: 4 });
    items.push(...figureIso(ISO.toScreen(...s.c), s.phase, s.moving, { scale: 1.9, body: "#f1e6cc", face: "#2a1d1a", hat: "#1b1a3a" }));
    return {
      background: [0.008, 0.007, 0.006],
      camera: motionCamera(s.cam),
      items,
      post: { bloom: 0.1, vignette: 0.3, grain: 0, passes: [paint ? { pass: "paint", preset: "oil-grotesque" } : { pass: "film", preset: "500t-print", ev: 1 }] },
    };
  },
};

export default {
  title: "Painted isometric RPG (template scaffold, playable)",
  duration: 12,
  fps: 30,
  game,
  async load(ctx) {
    // Tiles are 64 x 64: a diamond top in the lower half and, for blocks, a raised face.
    const set = makeTileset({ tileW: TW, tileH: 64, kinds: 4, seed: "iso-set", kind: (k, v, x, y, n) => {
      const top = Math.abs(x - 31.5) / 32 + Math.abs(y - 47.5) / 16 <= 1;
      const block = k > 1 && y < 48 && y > 48 - 12 - v * 3 && Math.abs(x - 31.5) < 31;
      if (!top && !block) return [0, 0, 0, 0];
      return [...shade(KINDS[k], (top ? 1 : 0.6) * (0.75 + 0.5 * n)), 255];
    } });
    if (ctx.motion) ctx.motion.image("iso-set", set);
    const lv = makeLevel({ w: N, h: N, seed: "iso-level", kindOf: (f) => (f < 0.35 ? 0 : f < 0.55 ? 2 : f < 0.75 ? 1 : 3) });
    return { kinds: lv.kinds, map: tileMap({ w: N, h: N, tileW: TW, tileH: TH, tsTileH: 64, gids: lv.gids, columns: 8, kinds: 4, orientation: "isometric" }) };
  },
  frame(t, ctx) { return filmFrame(game, ctx, t); },
};
