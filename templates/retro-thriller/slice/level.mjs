// Low Water, the ferry shelter: the vertical slice's place, authored by hand in code.
// A waiting room with a stove, a door to the quay, lamp posts, bollards, a tarp on a frame,
// and the harbour water below the kerb. Original placeholder art (ADR 0016); the layout is
// the level design, the tiles are stand-ins for painted ones.
//
//   cols  0..47, rows 0..27, 24 scene units a tile (2 m is 96 units: 48 units a metre)
//   rows 0-4 town wall | shelter cols 2-17 rows 5-15 (door at col 17, rows 9-11)
//   quay cols 1-46 rows 5-19 outside the shelter | kerb row 20 | water rows 21-27
import { makeTileset, tileMap, shade } from "../../common/procedural.mjs";

export const T = 24, W = 48, H = 28, UNITS_PER_M = 48;
export const K = { boards: 0, flags: 1, wall: 2, water: 3, kerb: 4, window: 5, rug: 6 };
const COLOURS = ["#5a4434", "#3d4144", "#16171a", "#0f2026", "#6a6a66", "#2b3640", "#5e2a2a"];

export const SHELTER = [2 * T, 5 * T, 16 * T, 11 * T];        // outer box of the waiting room
export const DOOR = { col: 17, rows: [9, 11], x: 17 * T, y0: 9 * T, y1: 12 * T };
export const STOVE = [5 * T + 12, 7 * T + 12];
export const LAMP_POSTS = [[24 * T + 12, 8 * T + 12], [38 * T + 12, 8 * T + 12], [30 * T + 12, 18 * T + 12]];
export const BOLLARDS = [[21, 20], [27, 20], [33, 20], [41, 20]].map(([c, r]) => [c * T + 12, r * T + 4]);
export const TARP = [34 * T, 6 * T];
export const PLANT_IN = [15 * T + 12, 6 * T + 14], PLANT_OUT = [44 * T, 7 * T];
export const BENCH = [6, 12, 9, 1];                            // cols 6-14, row 12
export const WATER_Y = 21 * T;

export function kindAt(c, r, doorOpen = true) {
  if (r < 5) return K.wall;
  if (r === 20) return K.kerb;
  if (r > 20) return K.water;
  if (c >= 2 && c <= 17 && r >= 5 && r <= 15) {
    const edge = c === 2 || c === 17 || r === 5 || r === 15;
    if (edge) {
      if (c === 17 && r >= 9 && r <= 11) return doorOpen ? K.boards : K.wall;
      if (r === 15 && ((c >= 5 && c <= 7) || (c >= 11 && c <= 13))) return K.window;
      return K.wall;
    }
    return c >= 8 && c <= 12 && r >= 8 && r <= 10 ? K.rug : K.boards;
  }
  return K.flags;
}

// Solid cells: walls, windows, water, and the props that stand on the floor.
export function solidAt(x, y, doorOpen) {
  const c = Math.floor(x / T), r = Math.floor(y / T);
  if (c < 0 || r < 0 || c >= W || r >= H) return true;
  const k = kindAt(c, r, doorOpen);
  if (k === K.wall || k === K.window || k === K.water) return true;
  if (r === BENCH[1] && c >= BENCH[0] && c < BENCH[0] + BENCH[2]) return true;
  const near = (p, rad) => Math.hypot(x - p[0], y - p[1]) < rad;
  return near(STOVE, 20) || LAMP_POSTS.some((p) => near(p, 8)) || BOLLARDS.some((p) => near(p, 9));
}
export const inShelter = ([x, y]) => x >= SHELTER[0] && x < SHELTER[0] + SHELTER[2] && y >= SHELTER[1] && y < SHELTER[1] + SHELTER[3];

// Tiles: 8 variants each, drawn as boards, flagstones, wall, water, kerb, glazed window, rug.
export function tileset() {
  return makeTileset({ tileW: T, tileH: T, kinds: 7, seed: "low-water-set", kind: (k, v, x, y, n) => {
    let f = 0.85 + 0.3 * n;
    if (k === K.boards) f *= (y % 8 === 0 ? 0.6 : 1) * (x === (v * 5) % 24 && y % 8 !== 0 ? 0.75 : 1);
    if (k === K.flags) f *= x === 0 || y === 0 || (v > 3 && x === 12) ? 0.55 : 1;
    if (k === K.water) f = 0.7 + 0.5 * n * (y % 6 < 2 ? 1.2 : 0.8);
    if (k === K.kerb) f *= y > 18 ? 0.55 : y < 3 ? 1.15 : 1;
    if (k === K.window) f *= x > 3 && x < 20 && y > 8 && y < 16 ? 2.1 : 0.6;
    if (k === K.rug) f *= (x + y) % 6 === 0 ? 1.35 : 1;
    return [...shade(COLOURS[k], f), 255];
  } });
}

export function map(doorOpen = true) {
  const gids = new Uint32Array(W * H);
  for (let r = 0; r < H; r++) for (let c = 0; c < W; c++) gids[r * W + c] = 1 + kindAt(c, r, doorOpen) * 8 + ((c * 7 + r * 13) % 8);
  return tileMap({ w: W, h: H, tileW: T, tileH: T, gids, columns: 8, kinds: 7 });
}
