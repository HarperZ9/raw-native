// node --test web/world/iso.test.mjs
// M2 criterion 9: picking round-trips every grid cell exactly; the fade mask behaves.
import { test } from "node:test";
import assert from "node:assert/strict";
import { isoCamera, fadeMask } from "./iso.mjs";

test("picking round-trips every cell of a 64 x 64 map, from its centre and near its corners", () => {
  for (const zoom of [1, 1.5, 3]) {
    const iso = isoCamera({ tileW: 64, tileH: 32, origin: [960, 100], zoom, width: 64, height: 64 });
    for (let j = 0; j < 64; j++) for (let i = 0; i < 64; i++) {
      for (const [dx, dy] of [[0.5, 0.5], [0.01, 0.01], [0.99, 0.01], [0.01, 0.99], [0.99, 0.99]]) {
        const [sx, sy] = iso.toScreen(i + dx, j + dy);
        assert.deepEqual(iso.pick(sx, sy), [i, j], `cell ${i},${j} at ${dx},${dy}, zoom ${zoom}`);
      }
    }
    // Off the map is null.
    assert.equal(iso.pick(...iso.toScreen(-0.5, 3)), null);
    assert.equal(iso.pick(...iso.toScreen(3, 64.5)), null);
  }
});

test("depth orders farther rows first and higher above lower", () => {
  const iso = isoCamera({ tileW: 64, tileH: 32 });
  assert.ok(iso.depth(1, 1) > iso.depth(0, 1));
  assert.ok(iso.depth(2, 2, 1) > iso.depth(2, 2, 0));
});

test("the fade mask fades only tall cells in front of the target that cover it on screen", () => {
  const w = 16, h = 16, heights = new Float32Array(w * h);
  const at = (i, j) => j * w + i;
  heights[at(6, 6)] = 3;   // a tall column in front of (5, 5)
  heights[at(4, 4)] = 3;   // a tall column behind it
  heights[at(12, 2)] = 3;  // in front by depth but off to the side on screen
  const iso = isoCamera({ tileW: 64, tileH: 32, origin: [512, 64] });
  const m = fadeMask({ width: w, height: h, heights }, iso, [5.5, 5.5], { radius: 1.5 });
  assert.ok(m[at(6, 6)] > 0.5, "the column in front fades");
  assert.equal(m[at(4, 4)], 0, "the column behind does not");
  assert.equal(m[at(12, 2)], 0, "a column that does not cover the target does not");
  assert.equal(m.filter((v) => v > 0).length, 1);
});
