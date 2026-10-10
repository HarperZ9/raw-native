// The CPU twin of the tile pass (web/motion/tiles_gpu.mjs): the same cell, tile, flip
// and texel arithmetic per pixel centre, premultiplied RGBA in [0, 1] over transparent.
//   tilesRef(map, image /* { width, height, data: RGBA8 } */, { ox, oy, s, opacity }, W, H)
export function tilesRef(map, image, { ox = 0, oy = 0, s = 1, opacity = 1 }, W, H) {
  const out = new Float32Array(W * H * 4), ts = map.tilesets[0];
  const cols = Math.max(1, ts.columns || Math.floor(image.width / map.tileW)), sp = ts.spacing || 0, mg = ts.margin || 0;
  for (const L of map.layers) {
    if (!L.visible) continue;
    const op = opacity * (L.opacity ?? 1);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const mx = (x + 0.5 - ox) / s, my = (y + 0.5 - oy) / s;
      if (mx < 0 || my < 0) continue;
      const cx = Math.floor(mx / map.tileW), cy = Math.floor(my / map.tileH);
      if (cx >= map.width || cy >= map.height) continue;
      const i = cy * map.width + cx, gid = L.gids[i];
      if (gid < ts.firstgid) continue;
      const local = gid - ts.firstgid;
      let fx = Math.floor(mx) - cx * map.tileW, fy = Math.floor(my) - cy * map.tileH;
      const fl = L.flip[i];
      if (fl & 4) [fx, fy] = [fy, fx];
      if (fl & 1) fx = map.tileW - 1 - fx;
      if (fl & 2) fy = map.tileH - 1 - fy;
      const tx = mg + (local % cols) * (map.tileW + sp) + fx, ty = mg + Math.floor(local / cols) * (map.tileH + sp) + fy;
      if (tx >= image.width || ty >= image.height) continue;
      const j = 4 * (ty * image.width + tx), a = image.data[j + 3] / 255, o = 4 * (y * W + x);
      const c = [image.data[j] / 255 * a * op, image.data[j + 1] / 255 * a * op, image.data[j + 2] / 255 * a * op, a * op];
      for (let k = 0; k < 4; k++) out[o + k] = c[k] + out[o + k] * (1 - c[3]);
    }
  }
  return out;
}
