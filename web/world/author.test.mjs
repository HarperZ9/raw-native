// node --test web/world/author.test.mjs
// Hand-authored tiles and maps (author.mjs): exact pixels from character rows, the atlas
// layout, map cells and flips, the drawn result through the tile pass's CPU twin, parallax
// depth against the Motion camera, the palette check, and that every malformed input names
// what is wrong.
import { test } from "node:test";
import assert from "node:assert/strict";
import { pixelTile, mirror, tileAtlas, mapLayer, authoredMap, parallaxZ, offPalette, rgba, AuthorError } from "./author.mjs";
import { tilesRef } from "./tiles_ref.mjs";
import { isoCamera } from "./iso.mjs";

const KEY = { ".": null, a: "#102030", b: [200, 100, 50], c: "#fff8" };
const px = (t, x, y) => Array.from(t.data.subarray(4 * (y * t.width + x), 4 * (y * t.width + x) + 4));

test("a pixel tile has exactly the keyed colours, clear where the key says null", () => {
  const t = pixelTile(["a.b", "bca"], KEY);
  assert.equal(t.width, 3); assert.equal(t.height, 2);
  assert.deepEqual(px(t, 0, 0), [16, 32, 48, 255]);
  assert.deepEqual(px(t, 1, 0), [0, 0, 0, 0]);
  assert.deepEqual(px(t, 2, 0), [200, 100, 50, 255]);
  assert.deepEqual(px(t, 1, 1), [255, 255, 255, 136]);
  assert.deepEqual(rgba("#abc"), [170, 187, 204, 255]);
});

test("mirror flips a tile left to right and top to bottom", () => {
  const t = pixelTile(["ab", "..",], KEY), h = mirror(t), v = mirror(t, { x: false, y: true });
  assert.deepEqual(px(h, 0, 0), px(t, 1, 0)); assert.deepEqual(px(h, 1, 0), px(t, 0, 0));
  assert.deepEqual(px(v, 0, 1), px(t, 0, 0)); assert.deepEqual(px(v, 0, 0), [0, 0, 0, 0]);
});

test("the atlas packs tiles in order, gids from 1, rows of `columns`", () => {
  const A = pixelTile(["aa", "aa"], KEY), B = pixelTile(["bb", "bb"], KEY), C = pixelTile(["cc", "cc"], KEY);
  const at = tileAtlas([["A", A], ["B", B], ["C", C]], { tileW: 2, tileH: 2, columns: 2 });
  assert.deepEqual(at.gid, { A: 1, B: 2, C: 3 });
  assert.equal(at.image.width, 4); assert.equal(at.image.height, 4); assert.equal(at.columns, 2);
  assert.deepEqual(px(at.image, 3, 1), [200, 100, 50, 255]);   // B, second column
  assert.deepEqual(px(at.image, 1, 3), [255, 255, 255, 136]);  // C, second row
  assert.deepEqual(px(at.image, 3, 3), [0, 0, 0, 0]);          // the unused slot stays clear
});

test("map layers place tiles cell by cell with flips; the drawn map matches the authored tiles", () => {
  const A = pixelTile(["ab", "ab"], KEY), B = pixelTile(["bb", "bb"], KEY);
  const at = tileAtlas({ A, B }, { tileW: 2, tileH: 2 });
  const map = authoredMap({ atlas: at, layers: [
    { name: "ground", rows: ["AB", "aA"], legend: { A: "A", B: "B", a: { tile: "A", flip: "h" } } },
    { name: "over", rows: [".B", ".."], legend: { ".": null, B: "B" }, opacity: 0.5 },
  ] });
  assert.equal(map.width, 2); assert.equal(map.height, 2);
  assert.deepEqual([...map.layers[0].gids], [1, 2, 1, 1]);
  assert.deepEqual([...map.layers[0].flip], [0, 0, 1, 0]);
  assert.deepEqual([...map.layers[1].gids], [0, 2, 0, 0]);
  const img = tilesRef(map, at.image, { s: 1 }, 4, 4), at8 = (x, y) => [0, 1, 2].map((k) => Math.round(img[4 * (y * 4 + x) + k] * 255));
  assert.deepEqual(at8(0, 0), [16, 32, 48]);     // A, unflipped: a then b
  assert.deepEqual(at8(1, 0), [200, 100, 50]);
  assert.deepEqual(at8(0, 2), [200, 100, 50]);   // A flipped: b then a
  assert.deepEqual(at8(1, 2), [16, 32, 48]);
});

test("an isometric authored map draws each cell's diamond where iso.mjs puts the cell", () => {
  const W = 8, H = 4, tile = (ch) => pixelTile(Array.from({ length: H }, () => ch.repeat(W)), KEY);
  const at = tileAtlas([["A", tile("a")], ["B", tile("b")]], { tileW: W, tileH: H });
  const map = authoredMap({ atlas: at, orientation: "isometric", layers: [{ rows: ["AB", "BA"], legend: { A: "A", B: "B" } }] });
  const ox = 16, oy = 2, img = tilesRef(map, at.image, { ox, oy, s: 1 }, 32, 16), iso = isoCamera({ tileW: W, tileH: H, origin: [ox, oy] });
  const colourAt = (i, j) => { const [x, y] = iso.toScreen(i + 0.5, j + 0.5), o = 4 * (Math.floor(y) * 32 + Math.floor(x)); return Math.round(img[o] * 255); };
  assert.equal(colourAt(0, 0), 16); assert.equal(colourAt(1, 0), 200); assert.equal(colourAt(0, 1), 200); assert.equal(colourAt(1, 1), 16);
});

test("parallaxZ gives the depth whose scroll rate under the Motion camera is the factor", () => {
  for (const [f, d] of [[0.25, 1000], [0.5, 1000], [1, 1000], [1.6, 800]]) {
    const z = parallaxZ(f, d);
    assert.ok(Math.abs(d / (d + z) - f) < 1e-12, `${f}`);
  }
  assert.ok(parallaxZ(0.5) > 0); assert.ok(parallaxZ(2) < 0);
});

test("offPalette finds pixels outside a fixed palette and ignores clear ones", () => {
  const t = pixelTile(["ab.", "ba."], { ".": null, a: "#102030", b: "#c86432" });
  assert.equal(offPalette(t, ["#102030", "#c86432"]).total, 0);
  const off = offPalette(t, ["#102030"]);
  assert.equal(off.total, 2); assert.deepEqual(off[0], { x: 1, y: 0, rgb: [200, 100, 50] });
});

test("malformed input throws an AuthorError that names the fault", () => {
  const bad = [
    [() => pixelTile(["ab", "a"], KEY), /row 1 has 1 characters/],
    [() => pixelTile(["az"], KEY), /column 1: character "z" is not in the key/],
    [() => pixelTile(["a"], { a: "#12" }), /expected #rgb/],
    [() => pixelTile([], KEY), /non-empty/],
    [() => tileAtlas([["A", pixelTile(["a"], KEY)]], { tileW: 2, tileH: 2 }), /tile A is 1 x 1, expected 2 x 2/],
    [() => tileAtlas([["A", pixelTile(["a"], KEY)], ["A", pixelTile(["a"], KEY)]], { tileW: 1, tileH: 1 }), /used twice/],
    [() => mapLayer(["AQ"], { A: "A" }, { gid: { A: 1 } }), /"Q" is not in the legend/],
    [() => mapLayer(["A"], { A: "missing" }, { gid: { A: 1 } }), /tile missing is not in the atlas/],
    [() => authoredMap({ atlas: { gid: { A: 1 } }, layers: [{ rows: ["A"], legend: { A: "A" } }, { rows: ["AA"], legend: { A: "A" } }] }), /is 2 x 1/],
    [() => authoredMap({ atlas: { gid: {} }, layers: [{ rows: ["."], legend: { ".": null } }], orientation: "hex" }), /orientation hex/],
    [() => parallaxZ(0), /positive/],
    [() => parallaxZ(-1), /positive/],
  ];
  for (const [f, re] of bad) assert.throws(f, (e) => e instanceof AuthorError && re.test(e.message), String(re));
});
