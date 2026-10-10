// Template (b), ADR 0016: "pixel art, 2.5D, somewhat semi 3D". Scaffold: placeholder art
// from code. A ground layer and a prop layer at different depths, so the camera's
// parallax gives the semi-3D feel, quantised to the PICO-8 palette with ordered dither.
import { makeTileset, makeLevel, tileMap, shade } from "../common/procedural.mjs";

const T = 16, W = 96, H = 54;
const GROUND = ["#1d2b53", "#c2c3c7", "#008751", "#5f574f"];   // water, sand, grass, stone (PICO-8 colours)
const PROPS = ["#00e436", "#ab5236"];                             // shrubs, rocks

export default {
  title: "Pixel 2.5D (template scaffold)",
  duration: 12,
  fps: 30,
  async load(ctx) {
    const ground = makeTileset({ tileW: T, tileH: T, kinds: 4, seed: "pixel-ground",
      kind: (k, v, x, y, n) => [...shade(GROUND[k], 0.8 + 0.4 * n + (k === 0 ? 0.15 * Math.sin((x + v * 3) * 0.8) : 0)), 255] });
    const props = makeTileset({ tileW: T, tileH: T, kinds: 2, seed: "pixel-props",
      kind: (k, v, x, y, n) => { const d = Math.hypot(x - 7.5, y - 9 + v * 0.2) < 5 + v * 0.3; return d ? [...shade(PROPS[k], 0.7 + 0.6 * n), 255] : [0, 0, 0, 0]; } });
    if (ctx.motion) { ctx.motion.image("pixel-ground", ground); ctx.motion.image("pixel-props", props); }
    const lv = makeLevel({ w: W, h: H, seed: "pixel-level", kindOf: (f) => (f < 0.32 ? 0 : f < 0.4 ? 1 : f < 0.72 ? 2 : 3) });
    const pv = makeLevel({ w: W, h: H, seed: "pixel-props", kindOf: (f, x, y) => { const k = lv.kinds[y * W + x]; return k === 2 && f > 0.62 ? 0 : k === 3 && f > 0.55 ? 1 : -1; } });
    return {
      ground: tileMap({ w: W, h: H, tileW: T, tileH: T, gids: lv.gids, columns: 8, kinds: 4 }),
      props: tileMap({ w: W, h: H, tileW: T, tileH: T, gids: pv.gids, columns: 8, kinds: 2, name: "props" }),
    };
  },
  frame(t, ctx) {
    const A = ctx.assets, x = 700 + 40 * t;
    return {
      background: [0.02, 0.02, 0.05],
      camera: { x, y: 430 + 20 * Math.sin(t * 0.4), zoom: 1, distance: 1000 },
      items: [
        { kind: "tilemap", map: A.ground, tileset: "pixel-ground", x: 0, y: 0, scale: 2, z: 0 },
        { kind: "tilemap", map: A.props, tileset: "pixel-props", x: 0, y: -6, scale: 2, z: -120 },   // nearer: moves faster
      ],
      post: { bloom: 0, vignette: 0.15, grain: 0, passes: [{ pass: "dither", palette: "pico8", mode: "bayer4" }] },
    };
  },
};
