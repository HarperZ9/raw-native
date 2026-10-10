// Valid seed inputs for the tile loaders: one map in every TMX encoding, and an LDtk
// level. The fuzzer mutates these; the tests check what they load to.
import { deflateSync, gzipSync } from "node:zlib";

const gids = [1, 2 | 0x80000000, 3 | 0x40000000, 0, 5, 6, 7, 8, 0, 0, 1, 2];
const bytes = new Uint8Array(new Uint32Array(gids).buffer);
const b64 = (b) => Buffer.from(b).toString("base64");
const tmx = (data) => `<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" orientation="orthogonal" renderorder="right-down" width="4" height="3" tilewidth="16" tileheight="16" infinite="0">
 <tileset firstgid="1" name="dungeon" tilewidth="16" tileheight="16" tilecount="64" columns="8"><image source="dungeon.png" width="128" height="128"/></tileset>
 <layer id="1" name="Ground" width="4" height="3">${data}</layer>
</map>`;

export const SAMPLES = {
  tmx: [
    tmx(`<data encoding="csv">\n${gids.map((g) => g >>> 0).join(",")}\n</data>`),
    tmx(`<data encoding="base64">${b64(bytes)}</data>`),
    tmx(`<data encoding="base64" compression="zlib">${b64(deflateSync(bytes))}</data>`),
    tmx(`<data encoding="base64" compression="gzip">${b64(gzipSync(bytes))}</data>`),
    tmx(`<data>${gids.map((g) => `<tile gid="${g >>> 0}"/>`).join("")}</data>`),
  ],
  ldtk: [JSON.stringify({
    jsonVersion: "1.5.3",
    defs: { tilesets: [{ uid: 7, identifier: "Dungeon", relPath: "dungeon.png", tileGridSize: 16, __cWid: 8, __cHei: 8, spacing: 0, padding: 0 }] },
    levels: [{ identifier: "Level_0", layerInstances: [
      { __identifier: "Decor", __type: "Tiles", __gridSize: 16, __cWid: 4, __cHei: 3, __tilesetDefUid: 7, __opacity: 1, gridTiles: [{ px: [16, 16], src: [0, 16], f: 0, t: 8 }] },
      { __identifier: "Ground", __type: "AutoLayer", __gridSize: 16, __cWid: 4, __cHei: 3, __tilesetDefUid: 7, __opacity: 1,
        autoLayerTiles: [{ px: [0, 0], src: [0, 0], f: 0, t: 0 }, { px: [16, 16], src: [16, 16], f: 3, t: 9 }, { px: [48, 32], src: [0, 0], f: 1, t: 0 }] },
    ] }],
  })],
};
