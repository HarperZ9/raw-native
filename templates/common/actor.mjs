// Shared game pieces for the template scaffolds: a placeholder figure drawn from shapes, a
// walk toward a point, and a grid path search. Placeholder art in code, like procedural.mjs;
// the author's art direction replaces the figures (ADR 0016).
import { circle, transform, rotateAbout } from "../../web/motion/path.mjs";

const ellipse = (cx, cy, rx, ry, n = 32) => transform(circle(0, 0, 1, n), [rx, 0, 0, ry, cx, cy]);

// A figure seen from above: coat, shoulders swinging with the walk, head. facing in radians
// (0 faces +x), phase is the walk cycle, scale in scene units. Returns Motion items.
export function figureTopDown([x, y], facing, phase, { scale = 1, coat = "#2b2622", skin = "#b89a80", stride = 1 } = {}) {
  const s = scale, sway = Math.sin(phase) * 0.18 * stride;
  const rot = (shape) => rotateAbout(shape, facing + Math.PI / 2, x, y);
  return [
    { shape: ellipse(x + 1.5 * s, y + 2 * s, 10 * s, 7 * s), fill: [0, 0, 0, 0.35], blur: 3 * s },                      // a contact shadow
    { shape: rot(rotateAbout(ellipse(x, y, 9 * s, 5 * s), sway, x, y)), fill: coat },
    { shape: rot(ellipse(x, y - 1.5 * s, 4.2 * s, 4.6 * s)), fill: skin },
    { shape: rot(ellipse(x, y + 0.2 * s, 4.4 * s, 3.2 * s)), fill: "#1a1714" },                                         // hair, from above
  ];
}

// A figure standing on an isometric floor at screen point [x, y] (its feet): a lean body
// with a bob, for the painted iso template. Returns Motion items.
export function figureIso([x, y], phase, moving, { scale = 1, body = "#3a2e5c", face = "#e7c9a5", hat = "#c4503a" } = {}) {
  const s = scale, bob = moving ? Math.abs(Math.sin(phase)) * 2.5 * s : 0, lean = moving ? Math.sin(phase) * 0.06 : 0;
  const top = y - 30 * s - bob;
  return [
    { shape: ellipse(x, y, 11 * s, 5 * s), fill: [0, 0, 0, 0.4], blur: 2 * s },
    { shape: rotateAbout([...ellipse(x, y - 15 * s - bob, 7 * s, 15 * s)], lean, x, y), fill: body },
    { shape: ellipse(x, top, 6 * s, 6.5 * s), fill: face },
    { shape: ellipse(x, top - 5 * s, 7.5 * s, 3 * s), fill: hat },
  ];
}

// Move pos toward goal at speed (units per second); returns [newPos, arrived].
export function walkToward(pos, goal, speed, dt) {
  const dx = goal[0] - pos[0], dy = goal[1] - pos[1], d = Math.hypot(dx, dy), stepLen = speed * dt;
  if (d <= stepLen) return [[goal[0], goal[1]], true];
  return [[pos[0] + (dx / d) * stepLen, pos[1] + (dy / d) * stepLen], false];
}

// Breadth-first path on a w x h grid of walkable cells (4-connected). Returns the cells from
// the one after start to goal, or null. Deterministic: neighbours in a fixed order.
export function gridPath(w, h, walkable, [si, sj], [gi, gj]) {
  if (!walkable(gi, gj)) return null;
  const prev = new Int32Array(w * h).fill(-1), q = [sj * w + si];
  prev[sj * w + si] = sj * w + si;
  for (let k = 0; k < q.length; k++) {
    const c = q[k], i = c % w, j = (c - i) / w;
    if (i === gi && j === gj) break;
    for (const [di, dj] of [[1, 0], [0, 1], [-1, 0], [0, -1]]) {
      const ni = i + di, nj = j + dj, n = nj * w + ni;
      if (ni < 0 || nj < 0 || ni >= w || nj >= h || prev[n] !== -1 || !walkable(ni, nj)) continue;
      prev[n] = c; q.push(n);
    }
  }
  const g = gj * w + gi;
  if (prev[g] === -1) return null;
  const out = [];
  for (let c = g; c !== sj * w + si; c = prev[c]) out.push([c % w, Math.floor(c / w)]);
  return out.reverse();
}
