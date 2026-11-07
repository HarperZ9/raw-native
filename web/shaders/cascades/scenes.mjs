// Flatland scenes for the radiance cascades pass and its brute-force reference.
//   const s = buildScene("room", 3840, 2160);
//   s.grid: Float32Array W*H*4 (radiance r, g, b, extinction sigma per pixel)
//   s.circles: analytic circles, { kind: "emit" | "glass", cx, cy, r, L: [r, g, b], n }
// A ray crossing a pixel for length l adds T * L * (1 - exp(-sigma l)) and keeps T * exp(-sigma l).
// Walls are sigma = OPAQUE: one pixel passes e^-1000. Analytic circles are exact: an "emit" circle is a
// solid emitter, a "glass" circle is a clear dielectric of index n that bends and splits rays.
// Shapes are given in units of the frame height, so one scene renders at any size.

export const OPAQUE = 1000;

export const SCENES = {
  // The planted check: one emitting disk in empty space. Its fluence has a closed form (diskFluence).
  disk: (H, a) => ({ shapes: [], circles: [{ kind: "emit", cx: 0.5 * a, cy: 0.5, r: 0.06, L: [1, 1, 1] }] }),
  // Thin walls (two pixels at 2160 lines), a doorway, coloured lights and a dark room behind a wall:
  // the leak case of ROADMAP P6. The right-hand room gets light only through its door.
  room: (H, a) => {
    const w = wallOf(H);
    return {
      shapes: [
        { x0: 0.62 * a, y0: 0.05, x1: 0.62 * a + w, y1: 0.42, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.62 * a, y0: 0.52, x1: 0.62 * a + w, y1: 0.95, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.62 * a, y0: 0.05, x1: 0.95 * a, y1: 0.05 + w, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.62 * a, y0: 0.95 - w, x1: 0.95 * a, y1: 0.95, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.95 * a - w, y0: 0.05, x1: 0.95 * a, y1: 0.95, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.10 * a, y0: 0.70, x1: 0.30 * a, y1: 0.73, L: [0.2, 0.9, 0.3], sigma: OPAQUE },
        { x0: 0.35 * a, y0: 0.20, x1: 0.37 * a, y1: 0.60, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.40 * a, y0: 0.80, x1: 0.55 * a, y1: 0.90, L: [0.05, 0.05, 0.08], sigma: 0.02 * 2160 / H },
      ],
      circles: [
        { kind: "emit", cx: 0.20 * a, cy: 0.30, r: 0.035, L: [3.0, 1.6, 0.6] },
        { kind: "emit", cx: 0.50 * a, cy: 0.15, r: 0.012, L: [1.0, 2.0, 6.0] },
      ],
    };
  },
  // H150: an analytic glass circle (n = 1.5) between lights and occluders. Errors are binned by the
  // distance of each pixel from the interface.
  glass: (H, a) => {
    const w = wallOf(H);
    return {
      shapes: [
        { x0: 0.12 * a, y0: 0.35, x1: 0.12 * a + 2 * w, y1: 0.65, L: [0, 0, 0], sigma: OPAQUE },
        { x0: 0.70 * a, y0: 0.82, x1: 0.92 * a, y1: 0.84, L: [0.9, 0.4, 0.2], sigma: OPAQUE },
      ],
      circles: [
        { kind: "glass", cx: 0.5 * a, cy: 0.5, r: 0.22, n: 1.5 },
        { kind: "emit", cx: 0.06 * a, cy: 0.5, r: 0.025, L: [4, 4, 3.6] },
        { kind: "emit", cx: 0.85 * a, cy: 0.18, r: 0.015, L: [0.5, 1.5, 4] },
      ],
    };
  },
};

function wallOf(H) { return Math.max(1, Math.round(H / 1080)) / H; }

export function buildScene(name, W, H) {
  if (!SCENES[name]) throw new Error(`unknown scene ${name}`);
  const def = SCENES[name](H, W / H), grid = new Float32Array(W * H * 4);
  for (const s of def.shapes) {
    const x0 = Math.max(0, Math.round(s.x0 * H)), x1 = Math.min(W, Math.round(s.x1 * H));
    const y0 = Math.max(0, Math.round(s.y0 * H)), y1 = Math.min(H, Math.round(s.y1 * H));
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
      const i = 4 * (y * W + x); grid[i] = s.L[0]; grid[i + 1] = s.L[1]; grid[i + 2] = s.L[2]; grid[i + 3] = s.sigma;
    }
  }
  const circles = def.circles.map((c) => ({ kind: c.kind, cx: c.cx * H, cy: c.cy * H, r: c.r * H, L: c.L || [0, 0, 0], n: c.n || 1 }));
  return { name, W, H, grid, circles };
}

// Fluence (radiance integrated over the full circle of directions) of an emitting disk of radiance L
// and radius R seen from distance d: 2 L asin(R / d) outside, 2 pi L inside.
export function diskFluence(L, R, d) { return d <= R ? 2 * Math.PI * L : 2 * L * Math.asin(R / d); }

// Distance of pixel centre (x + 0.5, y + 0.5) from the first glass circle's interface, or Infinity.
export function interfaceDistance(scene, x, y) {
  const g = scene.circles.find((c) => c.kind === "glass");
  if (!g) return Infinity;
  return Math.abs(Math.hypot(x + 0.5 - g.cx, y + 0.5 - g.cy) - g.r);
}

// Circles packed for the GPU: 8 floats each (cx, cy, r, kind, L or n in 4..6, spare).
export function packCircles(scene) {
  const out = new Float32Array(Math.max(1, scene.circles.length) * 8);
  scene.circles.forEach((c, i) => {
    out.set([c.cx, c.cy, c.r, c.kind === "glass" ? 1 : 0, c.kind === "glass" ? c.n : c.L[0], c.L[1], c.L[2], 0], 8 * i);
  });
  return out;
}
