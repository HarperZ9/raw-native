// node --test web/world/tiles.test.mjs
import { test } from "node:test";
import assert from "node:assert/strict";
import { deflateSync, gzipSync, inflateSync, gunzipSync } from "node:zlib";
import { parseTMX, parseLDtk, TileError, parseXML } from "./tiles.mjs";
import { SAMPLES } from "../../tools/fuzz/tile_samples.mjs";

const inflate = (b, kind) => new Uint8Array(kind === "gzip" ? gunzipSync(b) : inflateSync(b));

test("TMX: csv, base64, zlib, gzip and xml layers give the same cells, flips split off", () => {
  const maps = SAMPLES.tmx.map((s) => parseTMX(s, { inflate }));
  for (const m of maps) {
    assert.equal(m.width, 4); assert.equal(m.height, 3); assert.equal(m.tileW, 16);
    assert.deepEqual([...m.layers[0].gids], [1, 2, 3, 0, 5, 6, 7, 8, 0, 0, 1, 2]);
    assert.deepEqual([...m.layers[0].flip].slice(0, 3), [0, 1, 2]);
  }
  assert.equal(maps[0].tilesets[0].columns, 8);
});

test("LDtk: tiles land in their grid cells with flips, layers bottom first", () => {
  const m = parseLDtk(SAMPLES.ldtk[0]);
  assert.equal(m.width, 4); assert.equal(m.height, 3); assert.equal(m.tileW, 16);
  assert.deepEqual(m.layers.map((l) => l.name), ["Ground", "Decor"]);
  assert.equal(m.layers[0].gids[0], 1); assert.equal(m.layers[0].gids[5], 1 + 9); assert.equal(m.layers[0].flip[5], 3);
  assert.equal(m.tilesets[0].count, 64);
});

test("malformed input throws a TileError naming the problem", () => {
  for (const bad of ["", "<map", '<map width="0" height="2" tilewidth="16" tileheight="16"></map>', '<map width="2" height="2" tilewidth="16" tileheight="16"><layer><data encoding="csv">1,2</data></layer></map>', "<a><b></a>"])
    assert.throws(() => parseTMX(bad), TileError);
  for (const bad of ["{", "{}", '{"levels":[{}]}', '{"levels":[{"layerInstances":[{"__type":"Tiles","__gridSize":16,"__cWid":2,"__cHei":2,"gridTiles":[{"px":[99,0],"t":0}]}]}]}'])
    assert.throws(() => parseLDtk(bad), TileError);
  assert.equal(parseXML("<a x='1'>t<!-- c --><b/></a>").children[0].children[0].name, "b");
});

test("a map larger than the cell limit is refused before allocation", () => {
  assert.throws(() => parseTMX('<map width="65536" height="65536" tilewidth="16" tileheight="16"></map>'), /exceeds/);
});
void deflateSync; void gzipSync;
