// The playable templates (runtime G1): their demos move the player, walls and blocks hold,
// clicks path to the picked cell, and the film is a pure function of t.
import test from "node:test";
import assert from "node:assert/strict";
import thriller from "./retro-thriller/scene.mjs";
import iso from "./painted-iso-rpg/scene.mjs";
import { createSession } from "../web/game/loop.mjs";
import { createActionMap, frame } from "../web/game/input.mjs";
import { toDesign } from "../web/game/cameras.mjs";
import { isoCamera } from "../web/world/iso.mjs";

const run = async (scene, steps, input) => {
  const ctx = { motion: null, assets: null }; ctx.assets = await scene.load(ctx);
  const map = createActionMap(scene.game.actions), s = createSession(scene.game, { assets: ctx.assets });
  const seen = [];
  s.stepN(steps, (i) => { seen.push(s.state); return input ? input(i, map, s.state) : scene.game.demo(i, map); });
  seen.push(s.state);
  return { ctx, s, map, seen };
};

test("retro thriller: the demo walks the corridor and never leaves the floor rows", async () => {
  const { seen } = await run(thriller, 720);
  const xs = seen.map((st) => st.p[0]), ys = seen.map((st) => st.p[1]);
  assert.ok(Math.max(...xs) - Math.min(...xs) > 250, "walked east and back");
  for (const y of ys) assert.ok(y >= 6 * 24 + 8 && y <= 22 * 24 - 8, `y ${y} inside the corridor`);
  assert.ok(seen.at(-1).cam.zoom > 2.2, "zoomed in at the end");
  // Holding up forever stops at the wall.
  const { s } = await run(thriller, 600, (i, map) => frame(map, { axes: { moveY: -1 } }));
  assert.equal(s.state.p[1], 6 * 24 + 8);
});

test("retro thriller: a click walks to the point under the pointer", async () => {
  const target = [700, 400];
  const { s } = await run(thriller, 900, (i, map, st) => (i === 1 ? frame(map, { pressed: ["walkTo"], pointer: toDesign(st.cam, target) }) : frame(map, {})));
  assert.ok(Math.hypot(s.state.p[0] - target[0], s.state.p[1] - target[1]) < 1e-6, `at ${s.state.p}`);
});

test("painted iso: a click paths around blocks to the picked cell; the stick never enters a block", async () => {
  const ISO = isoCamera({ tileW: 64, tileH: 32, origin: [960, 140], width: 24, height: 24 });
  const probe = await run(iso, 0);
  const A = probe.ctx.assets, ok = (i, j) => A.kinds[j * 24 + i] < 2;
  // The farthest walkable cell reachable from the start (any cell the path search reaches).
  const goal = ok(3, 20) ? [3, 20] : (() => { for (let j = 23; j >= 0; j--) for (let i = 0; i < 24; i++) if (ok(i, j)) return [i, j]; })();
  const { s, seen } = await run(iso, 1800, (i, map, st) => {
    const p = toDesign(st.cam, ISO.toScreen(goal[0] + 0.5, goal[1] + 0.5));
    return i === 1 ? frame(map, { pressed: ["walkTo"], pointer: p }) : frame(map, { pointer: p });
  });
  const path = seen[2].path;
  assert.ok(path.length > 0, "a path was found");
  for (const st of seen) assert.ok(ok(Math.floor(st.c[0]), Math.floor(st.c[1])), `stood in a block at ${st.c}`);
  assert.deepEqual(s.state.c, [goal[0] + 0.5, goal[1] + 0.5]);
  assert.deepEqual(s.state.hover, goal, "the cursor picked the cell under the pointer");
  const demo = await run(iso, 720);
  for (const st of demo.seen) assert.ok(ok(Math.floor(st.c[0]), Math.floor(st.c[1])));
  assert.ok(Math.hypot(demo.seen.at(-1).c[0] - demo.seen[0].c[0], demo.seen.at(-1).c[1] - demo.seen[0].c[1]) > 2, "the demo moved");
});

test("both templates: frame(t) is the same however the film is seeked", async () => {
  for (const scene of [thriller, iso]) {
    const a = { motion: null }; a.assets = await scene.load(a);
    const b = { motion: null, assets: a.assets };
    const ts = [0, 1.5, 4.25, 6, 11.9];
    const fa = ts.map((t) => JSON.stringify(scene.frame(t, a).camera));
    const fb = [...ts].reverse().map((t) => JSON.stringify(scene.frame(t, b).camera)).reverse();
    assert.deepEqual(fa, fb, scene.title);
    assert.ok(scene.frame(6, a).items.length >= 4);
  }
});
