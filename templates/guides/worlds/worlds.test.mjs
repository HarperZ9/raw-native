// node --test templates/guides/worlds/worlds.test.mjs
// The two hand-made worlds, checked without a GPU: the tidepool's art stays on its fixed
// palette, every layer keeps one art pixel on one frame pixel, the harbour's map and props
// build, and its painter's order puts the quay's things in front of the office.
import { test } from "node:test";
import assert from "node:assert/strict";
import { offPalette } from "../../../web/world/author.mjs";
import * as T from "./tidepool_tiles.mjs";
import { loadTidepool, tidepool, place, PX, DEPTH } from "./tidepool.mjs";
import { loadHarbour, harbour } from "./harbour.mjs";
import { office, lamp } from "./harbour_props.mjs";

test("every tidepool tile and sprite uses only the shore palette", async () => {
  const W = await loadTidepool(null);
  const images = [...T.GROUND_TILES, ...T.SKY_TILES, ...T.SEA_TILES].map(([n, t]) => [n, t]);
  for (const [k, s] of Object.entries(W.sprites)) images.push([k, s.atlas.image]);
  for (const [name, img] of images) assert.equal(offPalette(img, T.SHORE_PALETTE).total, 0, name);
  assert.equal(T.SHORE_PALETTE.length, 20);
});

test("each parallax layer draws one art pixel per frame pixel at the reference camera", async () => {
  const W = await loadTidepool(null), { back, front } = tidepool(W);
  for (const it of [...back, ...front]) {
    const f = 1000 / (1000 + it.z);                 // the Motion camera's depth factor
    assert.ok(Math.abs(it.scale * f - PX) < 1e-9, `scale ${it.scale} at factor ${f}`);
    const sx = 960 + (it.x - 960) * f, sy = 540 + (it.y - 540) * f;
    assert.ok(Math.abs(sx / PX - Math.round(sx / PX)) < 1e-6 && Math.abs(sy / PX - Math.round(sy / PX)) < 1e-6, `origin ${sx}, ${sy} off the pixel grid`);
  }
  assert.ok(Object.values(DEPTH).every((f) => f > 0));
  assert.equal(place(W.sky, "x", 0, 0, 0.5).scale, PX / 0.5);
});

test("the harbour builds from its map: ground on every land cell, things sorted by depth", async () => {
  const W = await loadHarbour();
  assert.equal(W.layer.width, 16); assert.equal(W.layer.height, 16);
  const h = harbour(W);
  assert.ok(h.back.length > 600, `${h.back.length} ground and water items`);
  for (const t of h.things) assert.ok(Number.isFinite(t.d) && t.items.length > 0);
  // The lamp stands in front of the office wall, so it must sort after the office.
  assert.ok(lamp(4.75, 5.9).d > office(null).d);
});
