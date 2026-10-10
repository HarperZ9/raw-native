// Template (c), ADR 0016: a thriller with a retro look, "more like Disco Elysium and
// less like horror ... I want it to be scary ... not ... focused entirely on horror and
// on jump scares and fear". Scaffold: placeholder art from code. A dim corridor seen from
// above, one unsteady practical light, colour negative film for the stock and a soft
// tube for the screen it plays on. Dread comes from the light and the framing, not shocks.
import { makeTileset, makeLevel, tileMap, shade } from "../common/procedural.mjs";
import { circle } from "../../web/motion/path.mjs";

const T = 24, W = 48, H = 28;
const KINDS = ["#2a2f36", "#3c3a35", "#14161a"];                  // floor, carpet, wall

export default {
  title: "Retro thriller (template scaffold)",
  duration: 12,
  fps: 30,
  async load(ctx) {
    const set = makeTileset({ tileW: T, tileH: T, kinds: 3, seed: "thriller-set",
      kind: (k, v, x, y, n) => [...shade(KINDS[k], (k === 0 && (x === 0 || y === 0) ? 0.7 : 1) * (0.85 + 0.3 * n)), 255] });
    if (ctx.motion) ctx.motion.image("thriller-set", set);
    const lv = makeLevel({ w: W, h: H, seed: "thriller-level", kindOf: (f, x, y) => (y < 6 || y > 21 ? 2 : x > 18 && x < 30 ? 1 : 0) });
    return { map: tileMap({ w: W, h: H, tileW: T, tileH: T, gids: lv.gids, columns: 8, kinds: 3 }) };
  },
  frame(t, ctx) {
    const A = ctx.assets;
    // The lamp flickers: a slow sway plus two fast terms, never a strobe.
    const flick = 0.8 + 0.12 * Math.sin(t * 1.3) + 0.05 * Math.sin(t * 17.0) + 0.03 * Math.sin(t * 29.0);
    const lamp = [0.9 * flick, 0.65 * flick, 0.4 * flick, 1];
    return {
      background: [0.004, 0.004, 0.006],
      camera: { x: 576 + 30 * Math.sin(t * 0.2), y: 336, zoom: 1.6 - 0.02 * t },
      items: [
        { kind: "tilemap", map: A.map, tileset: "thriller-set", x: 0, y: 0, scale: 1, opacity: 0.9 },
        { shape: circle(600, 330, 260, 96), fill: { radial: [600, 330, 260], stops: [[0, lamp], [1, [0, 0, 0, 0]]] }, blend: "add" },
      ],
      post: { bloom: 0.25, threshold: 1.0, vignette: 0.5, grain: 0, colour: undefined,
        passes: [{ pass: "film", preset: "500t-print" }, { pass: "crt-classic", cell: 3, scanlines: true, scanStrength: 0.25, mask: "grille", maskStrength: 0.15, bloom: 0.1, curvature: 0.06, aberration: 0.08, vignette: 0.35 }] },
    };
  },
};
