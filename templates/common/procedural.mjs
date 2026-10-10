// Placeholder art for the template scaffolds: tilesets and levels made from code, so a
// scaffold runs before its real art exists. Every value comes from a seeded generator,
// so frames are the same on every run. The art direction is the author's to give
// (ADR 0016); these stand in for it and say so.
import { rng } from "../../third_party/superstack/superstack.mjs";

const hex = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16));

// A tileset of `kinds` rows of 8 variants, each tile tileW x tileH. kind(k, v, x, y, r)
// returns [r, g, b, a] in 0..255 for texel (x, y) of variant v of kind k.
export function makeTileset({ tileW = 16, tileH = 16, kinds, seed = "tiles", kind }) {
  const r = rng(seed), W = 8 * tileW, H = kinds * tileH, data = new Uint8Array(W * H * 4);
  const noise = Float32Array.from({ length: 4096 }, () => r.nextFloat());
  for (let k = 0; k < kinds; k++) for (let v = 0; v < 8; v++) for (let y = 0; y < tileH; y++) for (let x = 0; x < tileW; x++) {
    const n = noise[(k * 977 + v * 131 + y * tileW + x) & 4095];
    data.set(kind(k, v, x, y, n), 4 * ((k * tileH + y) * W + v * tileW + x));
  }
  return { width: W, height: H, data, columns: 8 };
}

// A level of w x h cells: kind of each cell from a smooth seeded field, variant random.
// kinds(level) maps a field value in [0, 1) to a kind index.
export function makeLevel({ w, h, seed = "level", kindOf }) {
  const r = rng(seed), grid = Float32Array.from({ length: 64 }, () => r.nextFloat());
  const field = (x, y) => {
    const gx = (x / w) * 7, gy = (y / h) * 7, ix = Math.floor(gx), iy = Math.floor(gy), fx = gx - ix, fy = gy - iy;
    const g = (i, j) => grid[(j % 8) * 8 + (i % 8)], s = (t) => t * t * (3 - 2 * t);
    return (g(ix, iy) * (1 - s(fx)) + g(ix + 1, iy) * s(fx)) * (1 - s(fy)) + (g(ix, iy + 1) * (1 - s(fx)) + g(ix + 1, iy + 1) * s(fx)) * s(fy);
  };
  const kinds = new Uint8Array(w * h), gids = new Uint32Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const k = kindOf(field(x, y), x, y);
    kinds[y * w + x] = k;
    gids[y * w + x] = k < 0 ? 0 : 1 + k * 8 + Math.floor(r.nextFloat() * 8);
  }
  return { kinds, gids, field };
}

export const tileMap = ({ w, h, tileW, tileH, gids, columns, kinds = 8, orientation = "orthogonal", name = "ground", opacity = 1, tsTileH = tileH }) => ({
  width: w, height: h, tileW, tileH, orientation,
  layers: [{ name, gids, flip: new Uint8Array(w * h), opacity, visible: true }],
  tilesets: [{ firstgid: 1, columns, count: columns * kinds, tileW, tileH: tsTileH, spacing: 0, margin: 0 }],
});

export const shade = (h, k) => { const c = hex(h); return [c[0] * k, c[1] * k, c[2] * k].map((v) => Math.max(0, Math.min(255, Math.round(v)))); };
