// Drawing the environment state in a 2D Motion scene (ADR 0017 readers, made visible).
// Every function takes the frozen snapshot (or a reader's output) and returns Motion items,
// so one weather change moves light, wet surfaces, rain, foliage and sight together.
// Placeholder art in code, like procedural.mjs; the author's art direction replaces it.
import { circle, rect, polyline, rotateAbout } from "../../web/motion/path.mjs";
import { hash01 } from "../../web/env/sky.mjs";

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// A region darkened toward black by amount (0 none, 1 black), drawn over the floor.
export const shade = ([x, y, w, h], amount) => (amount > 0.002 ? { shape: rect(x, y, w, h), fill: [0, 0, 0, clamp(amount, 0, 1)] } : null);

// The wet-surface response in 2D: the reader's darkening as a multiply-toward-black, and its
// sheen as reflections of each light stretched away from the viewer along +y, as a wet
// street shows them. Brighter and longer as the surface gets glossier (peakGain).
export function wetSurface(region, mat, lights) {
  const out = [shade(region, 1 - mat.darken)];
  if (mat.sheen < 0.05) return out;
  const gloss = clamp(Math.log2(mat.peakGain) / 8, 0, 1) * mat.sheen;
  const [x0, y0, w, h] = region;
  for (const L of lights) {
    if (L.x < x0 || L.x > x0 + w) continue;
    const len = Math.min(y0 + h - L.y, 60 + 200 * gloss), wide = 10 + 16 * (1 - gloss);
    if (len <= 0) continue;
    const c = L.colour, a = 0.55 * gloss * L.level;
    out.push({ shape: rect(L.x - wide / 2, L.y, wide, len, wide / 2), fill: { linear: [L.x, L.y, L.x, L.y + len], stops: [[0, [c[0], c[1], c[2], a]], [1, [c[0], c[1], c[2], 0]]] }, blend: "add", blur: 4 });
  }
  return out;
}

// Rain seen from above: splash rings where drops land and a few streaks slanted by the wind.
// Drops are hashed from (seed, drop, period), so the same time draws the same rain.
export function rainItems([x0, y0, w, h], rainRate, wind, t, seed = 11) {
  const n = Math.min(240, Math.round(rainRate * 7)), out = [];
  if (n === 0) return out;
  const period = 0.45, sx = clamp(wind[0] * 2.5, -40, 40), sy = clamp(wind[1] * 2.5, -40, 40);
  for (let i = 0; i < n; i++) {
    const ph = hash01(seed, 21, i) * period, k = Math.floor((t + ph) / period), f = ((t + ph) % period) / period;
    const x = x0 + w * hash01(seed, 22 + (k & 1023), i), y = y0 + h * hash01(seed, 23 + (k & 1023), i);
    if (f < 0.55) out.push({ shape: circle(x, y, 1 + 7 * f, 12), fill: null, stroke: [0.75, 0.8, 0.9, 0.35 * (1 - f / 0.55)], width: 1.1 });
    else if (i % 3 === 0) { const g = (f - 0.55) / 0.45; out.push({ shape: polyline([x - sx * (1 - g) - 2, y - 26 * (1 - g) + sy * (1 - g), x - sx * (1 - g) * 0.6, y - 18 * (1 - g)]), fill: null, stroke: [0.7, 0.75, 0.85, 0.25], width: 1 }); }
  }
  return out;
}

// A cloth hanging from a rail at (x, y), length L, width W, seen from above: its free edge
// swings by the sway angle (web/env/readers.mjs stepSway), with the bend growing down the cloth.
export function clothItems([x, y], { length = 70, width = 120, angle = 0, colour = "#5a2a24" } = {}) {
  const n = 7, left = [], right = [];
  for (let k = 0; k <= n; k++) {
    const s = k / n, bend = Math.sin(angle) * length * s * s, ripple = Math.sin(s * 5 + angle * 6) * 3 * Math.abs(angle) * s;
    left.push(x - width / 2 + bend * 0.6 + ripple, y + s * length * Math.cos(angle * 0.5));
    right.push(x + width / 2 + bend + ripple, y + s * length * Math.cos(angle * 0.5));
  }
  const pts = [...left];
  for (let k = n; k >= 0; k--) pts.push(right[2 * k], right[2 * k + 1]);
  // Folds: darker lines from the rail down the cloth, bent with it.
  const folds = [0.25, 0.5, 0.75].map((u) => {
    const line = [];
    for (let k = 0; k <= n; k++) line.push(left[2 * k] + (right[2 * k] - left[2 * k]) * u, left[2 * k + 1]);
    return { shape: polyline(line), fill: null, stroke: [0, 0, 0, 0.35], width: 2 };
  });
  return [{ shape: polyline([x - width / 2 - 4, y, x + width / 2 + 4, y]), fill: null, stroke: "#1a1714", width: 4 },
    { shape: polyline(pts, true), fill: colour, opacity: 0.92 }, ...folds];
}

// A potted plant from above: leaves around the pot, each rotated by the sway angle scaled by
// its own flexibility, so a gust moves the long fronds more than the short ones.
export function plantItems([x, y], angle, { leaves = 7, size = 26, colour = "#2f5a3a" } = {}) {
  const out = [{ shape: circle(x, y, size * 0.45, 24), fill: "#3b2b22" }];
  for (let i = 0; i < leaves; i++) {
    const a = (i / leaves) * 2 * Math.PI + 0.3, flex = 0.6 + 0.8 * ((i * 37) % 10) / 10, len = size * (0.8 + 0.5 * flex);
    const leaf = polyline([x, y, x + Math.cos(a - 0.18) * len * 0.55, y + Math.sin(a - 0.18) * len * 0.55, x + Math.cos(a) * len, y + Math.sin(a) * len, x + Math.cos(a + 0.18) * len * 0.55, y + Math.sin(a + 0.18) * len * 0.55], true);
    out.push({ shape: rotateAbout(leaf, angle * flex, x, y), fill: colour });
  }
  return out;
}

// What a character can see, as a vignette around them: clear to radius r, black past 1.7 r.
export function sightMask([x, y], r, { strength = 0.92, reach = 2400 } = {}) {
  if (r * 1.7 >= reach) return null;
  return { shape: circle(x, y, reach, 96), fill: { radial: [x, y, reach], stops: [[0, [0, 0, 0, 0]], [r / reach, [0, 0, 0, 0]], [(1.7 * r) / reach, [0, 0, 0, strength]], [1, [0, 0, 0, strength]]] } };
}
