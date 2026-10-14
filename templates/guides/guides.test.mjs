// node --test templates/guides/guides.test.mjs
// The pure half of the explainer guides: the 2.5D rig and the three characters. The renders
// themselves come from tools/guides/render_all.py.
import { test } from "node:test";
import assert from "node:assert/strict";
import { pose, hatch, seeded } from "./rig.mjs";
import { tallow } from "./tallow.mjs";
import { pylon } from "./pylon.mjs";
import { rerun } from "./rerun.mjs";
import { bounds } from "../../web/motion/path.mjs";

const CHARS = { tallow, pylon, rerun };
const box = (items) => {
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  // A clipped part draws only inside its clip, so its own bounds do not count.
  for (const it of items) { if (it.clip) continue; const b = bounds(it.shape); x0 = Math.min(x0, b.x0); x1 = Math.max(x1, b.x1); y0 = Math.min(y0, b.y0); y1 = Math.max(y1, b.y1); }
  return { x0, x1, y0, y1 };
};

test("every guide poses at every yaw with finite geometry and the feet on the ground line", () => {
  for (const [name, make] of Object.entries(CHARS)) for (const yaw of [0, 35, 90, 180]) {
    const items = pose(make({}), { x: 500, y: 900, s: 0.5, yaw });
    assert.ok(items.length > 10, `${name} at ${yaw}: ${items.length} items`);
    const b = box(items);
    for (const v of Object.values(b)) assert.ok(Number.isFinite(v), `${name} at ${yaw}: non-finite bounds`);
    assert.ok(Math.abs(b.y1 - 900) < 0.5 * 40, `${name} at ${yaw}: lowest point ${b.y1}, ground 900`);
  }
});

test("parts that face the viewer hide when the guide turns its back", () => {
  for (const [name, make] of Object.entries(CHARS)) {
    const front = pose(make({}), { yaw: 0 }).length, back = pose(make({}), { yaw: 180 }).length;
    assert.ok(back < front, `${name}: ${back} parts from behind, ${front} from the front`);
  }
});

test("a pose is pure: the same options give the same shapes", () => {
  const a = pose(tallow({ armR: 160, prop: "card" }), { yaw: 20 }), b = pose(tallow({ armR: 160, prop: "card" }), { yaw: 20 });
  assert.deepEqual(a.map((i) => Array.from(i.shape[0].pts.slice(0, 6))), b.map((i) => Array.from(i.shape[0].pts.slice(0, 6))));
  assert.equal(hatch(0, 0, 100, 100, { seed: "x" }), hatch(0, 0, 100, 100, { seed: "x" }));
  assert.equal(seeded("s")(), seeded("s")());
});

test("mirroring a pose mirrors its bounds about the anchor", () => {
  const a = box(pose(pylon({}), { x: 400, y: 800, s: 1, yaw: 30 })), b = box(pose(pylon({}), { x: 400, y: 800, s: 1, yaw: 30, flip: true }));
  assert.ok(Math.abs((a.x1 - 400) - (400 - b.x0)) < 1e-3 && Math.abs((400 - a.x0) - (b.x1 - 400)) < 1e-3);
});
