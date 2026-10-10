// Template (a), ADR 0016: a painted isometric RPG, "more Disco Elysium and less Rogue
// Trader", "very, very, very abstract, artistic, grotesque", "very, very indie", "make it
// real funky". Scaffold: placeholder art from code. An isometric district of blocks in
// scene light; the painterly pass turns it to thick, outlined oil when it is registered
// (raw-native PR #55), and colour negative film stands in until then.
import { makeTileset, makeLevel, tileMap, shade } from "../common/procedural.mjs";
import { registeredPasses } from "../../web/motion/post.mjs";

const TW = 64, TH = 32, N = 24;
const KINDS = ["#a0704a", "#c4503a", "#5f9a74", "#e8b84a"];       // earth, brick, moss, ochre

export default {
  title: "Painted isometric RPG (template scaffold)",
  duration: 12,
  fps: 30,
  async load(ctx) {
    // Tiles are 64 x 64: a diamond top in the lower half and, for blocks, a raised face.
    const set = makeTileset({ tileW: TW, tileH: 64, kinds: 4, seed: "iso-set", kind: (k, v, x, y, n) => {
      const top = Math.abs(x - 31.5) / 32 + Math.abs(y - 47.5) / 16 <= 1;
      const block = k > 1 && y < 48 && y > 48 - 12 - v * 3 && Math.abs(x - 31.5) < 31;
      if (!top && !block) return [0, 0, 0, 0];
      return [...shade(KINDS[k], (top ? 1 : 0.6) * (0.75 + 0.5 * n)), 255];
    } });
    if (ctx.motion) ctx.motion.image("iso-set", set);
    const lv = makeLevel({ w: N, h: N, seed: "iso-level", kindOf: (f) => (f < 0.35 ? 0 : f < 0.55 ? 2 : f < 0.75 ? 1 : 3) });
    return { map: tileMap({ w: N, h: N, tileW: TW, tileH: TH, tsTileH: 64, gids: lv.gids, columns: 8, kinds: 4, orientation: "isometric" }) };
  },
  frame(t, ctx) {
    const A = ctx.assets, paint = registeredPasses().includes("paint");
    return {
      background: [0.008, 0.007, 0.006],
      camera: { x: 960 + 50 * Math.sin(t * 0.25), y: 520, zoom: 1.1 },
      items: [{ kind: "tilemap", map: A.map, tileset: "iso-set", x: 960, y: 140, scale: 1 }],
      post: { bloom: 0.1, vignette: 0.3, grain: 0, passes: [paint ? { pass: "paint", preset: "oil-grotesque" } : { pass: "film", preset: "500t-print", ev: 1 }] },
    };
  },
};
